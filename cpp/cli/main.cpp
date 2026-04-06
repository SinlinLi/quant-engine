// quant-engine C++ CLI
// 纯 C++ 回测，无 Python 开销，吞吐量 ~5M bars/s
//
// Usage:
//   qe_cli backtest --strategy dual_ma --symbols BTCUSDT --start 2024-01-01 --end 2025-01-01
//   qe_cli backtest --strategy macd_cross --symbols BTCUSDT --start 2024-01-01 --end 2025-01-01 --interval 1d
//   qe_cli list-data

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "strategies/dual_ma.h"
#include "strategies/macd_cross.h"
#include "cli/ch_client.h"

using namespace qe;

// ── 参数解析 ──

struct Args {
    std::string command;                   // backtest | list-data
    std::string strategy = "dual_ma";
    std::vector<std::string> symbols;
    std::string start, end;
    std::string interval = "1m";
    double cash = 10000.0;
    double commission = 0.0004;
    double slippage = 0.0001;
    std::map<std::string, double> params;
    bool no_save = true;                   // C++ CLI 默认不写 ClickHouse
    // ClickHouse
    std::string ch_host = "localhost";
    int ch_port = 8123;
    std::string ch_user = "default";
    std::string ch_password;
};

static void print_usage() {
    printf(
        "Usage:\n"
        "  qe_cli backtest --strategy <name> --symbols <SYM...> --start <DATE> --end <DATE> [options]\n"
        "  qe_cli list-data\n"
        "\n"
        "Backtest options:\n"
        "  --strategy <name>    dual_ma, macd_cross (default: dual_ma)\n"
        "  --symbols <SYM...>   交易对 (空格分隔)\n"
        "  --start <DATE>       开始日期 YYYY-MM-DD\n"
        "  --end <DATE>         结束日期 YYYY-MM-DD\n"
        "  --interval <I>       K 线周期: 1m, 5m, 15m, 1h, 4h, 1d (default: 1m)\n"
        "  --cash <N>           初始资金 (default: 10000)\n"
        "  --commission <N>     手续费率 (default: 0.0004)\n"
        "  --slippage <N>       滑点 (default: 0.0001)\n"
        "  --params <k=v ...>   策略参数\n"
        "\n"
        "ClickHouse options:\n"
        "  --ch-host <H>        (default: localhost)\n"
        "  --ch-port <P>        (default: 8123)\n"
        "  --ch-user <U>        (default: default)\n"
        "  --ch-password <P>    (default: env QE_CH_PASSWORD or ***REMOVED***)\n"
    );
}

static Args parse_args(int argc, char** argv) {
    Args a;

    // 默认密码从环境变量读取
    const char* env_pw = getenv("QE_CH_PASSWORD");
    a.ch_password = env_pw ? env_pw : "***REMOVED***";

    if (argc < 2) { print_usage(); exit(1); }
    a.command = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "Missing value for %s\n", arg.c_str()); exit(1); }
            return argv[++i];
        };

        if (arg == "--strategy")    a.strategy = next();
        else if (arg == "--start")  a.start = next();
        else if (arg == "--end")    a.end = next();
        else if (arg == "--interval") a.interval = next();
        else if (arg == "--cash")   a.cash = std::stod(next());
        else if (arg == "--commission") a.commission = std::stod(next());
        else if (arg == "--slippage") a.slippage = std::stod(next());
        else if (arg == "--ch-host") a.ch_host = next();
        else if (arg == "--ch-port") a.ch_port = std::stoi(next());
        else if (arg == "--ch-user") a.ch_user = next();
        else if (arg == "--ch-password") a.ch_password = next();
        else if (arg == "--symbols") {
            // 读取后续所有非 -- 开头的参数作为 symbols
            while (i + 1 < argc && argv[i + 1][0] != '-')
                a.symbols.push_back(argv[++i]);
        }
        else if (arg == "--params") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string kv = argv[++i];
                auto eq = kv.find('=');
                if (eq == std::string::npos) { fprintf(stderr, "Invalid param: %s\n", kv.c_str()); exit(1); }
                a.params[kv.substr(0, eq)] = std::stod(kv.substr(eq + 1));
            }
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage();
            exit(1);
        }
    }
    return a;
}

// ── 从 ClickHouse 加载 bars ──

static std::string interval_bucket(const std::string& interval) {
    if (interval == "5m")  return "toStartOfFiveMinutes(open_time)";
    if (interval == "15m") return "toStartOfFifteenMinutes(open_time)";
    if (interval == "1h")  return "toStartOfHour(open_time)";
    if (interval == "4h")  return "toStartOfInterval(open_time, INTERVAL 4 HOUR)";
    if (interval == "1d")  return "toStartOfDay(open_time)";
    return "";
}

static std::vector<Bar> load_bars(const CHConfig& cfg, const std::string& symbol,
                                  const std::string& start, const std::string& end,
                                  const std::string& interval) {
    std::string query;
    if (interval == "1m") {
        query = "SELECT toInt64(toUnixTimestamp64Milli(open_time)),"
                "open, high, low, close, volume, quote_volume "
                "FROM qe.klines_1m "
                "WHERE symbol = '" + symbol + "' "
                "AND open_time >= '" + start + "' AND open_time < '" + end + "' "
                "ORDER BY open_time";
    } else {
        auto bucket = interval_bucket(interval);
        if (bucket.empty()) {
            fprintf(stderr, "Unsupported interval: %s\n", interval.c_str());
            exit(1);
        }
        query = "SELECT toInt64(toUnixTimestamp64Milli(toDateTime64(bucket, 3, 'UTC'))),"
                "argMin(open, open_time), max(high), min(low),"
                "argMax(close, open_time), sum(volume), sum(quote_volume) "
                "FROM ("
                "  SELECT " + bucket + " AS bucket, open_time, open, high, low, close,"
                "         volume, quote_volume"
                "  FROM qe.klines_1m"
                "  WHERE symbol = '" + symbol + "'"
                "  AND open_time >= '" + start + "' AND open_time < '" + end + "'"
                ") GROUP BY bucket ORDER BY bucket";
    }

    auto rows = ch_query(cfg, query);

    std::vector<Bar> bars;
    bars.reserve(rows.size());
    for (auto& row : rows) {
        if (row.size() < 7) continue;
        Bar b;
        b.timestamp_ms = std::stoll(row[0]);
        b.open         = std::stod(row[1]);
        b.high         = std::stod(row[2]);
        b.low          = std::stod(row[3]);
        b.close        = std::stod(row[4]);
        b.volume       = std::stod(row[5]);
        b.quote_volume = std::stod(row[6]);
        bars.push_back(b);
    }
    return bars;
}

// ── 策略工厂 ──

static std::shared_ptr<Strategy> make_strategy(
        const std::string& name,
        const std::vector<std::string>& symbols,
        const std::map<std::string, double>& params) {

    auto get = [&](const std::string& key, double def) -> double {
        auto it = params.find(key);
        return it != params.end() ? it->second : def;
    };

    if (name == "dual_ma") {
        int fast = (int)get("fast", 5);
        int slow = (int)get("slow", 20);
        return std::make_shared<DualMA>(symbols[0], fast, slow);
    }
    if (name == "macd_cross") {
        int fast   = (int)get("fast", 12);
        int slow   = (int)get("slow", 26);
        int signal = (int)get("signal", 9);
        return std::make_shared<MACDCross>(symbols[0], fast, slow, signal);
    }

    fprintf(stderr, "Unknown strategy: %s\n", name.c_str());
    exit(1);
}

// ── backtest ──

static void cmd_backtest(const Args& a) {
    CHConfig cfg{a.ch_host, a.ch_port, a.ch_user, a.ch_password};
    Engine engine;

    if (a.symbols.empty()) {
        fprintf(stderr, "Error: --symbols required\n");
        exit(1);
    }
    if (a.start.empty() || a.end.empty()) {
        fprintf(stderr, "Error: --start and --end required\n");
        exit(1);
    }

    // 加载数据
    auto t_load_start = std::chrono::steady_clock::now();
    for (auto& symbol : a.symbols) {
        auto bars = load_bars(cfg, symbol, a.start, a.end, a.interval);
        if (bars.empty()) {
            fprintf(stderr, "Warning: no data for %s in [%s, %s)\n",
                    symbol.c_str(), a.start.c_str(), a.end.c_str());
            continue;
        }
        auto sid = engine.symbols().id(symbol);
        printf("Loaded %zu bars for %s (%s)\n", bars.size(), symbol.c_str(), a.interval.c_str());
        engine.add_feed(std::make_unique<CsvFeed>(sid, std::move(bars)));
    }
    auto t_load_end = std::chrono::steady_clock::now();

    // Broker
    SimBrokerConfig broker_cfg;
    broker_cfg.cash = a.cash;
    broker_cfg.commission_rate = a.commission;
    broker_cfg.slippage = a.slippage;
    engine.set_broker(std::make_unique<SimBroker>(broker_cfg));

    // 策略
    engine.add_strategy(make_strategy(a.strategy, a.symbols, a.params));

    // 运行
    auto t_run_start = std::chrono::steady_clock::now();
    auto result = engine.run();
    auto t_run_end = std::chrono::steady_clock::now();

    double load_ms = std::chrono::duration<double, std::milli>(t_load_end - t_load_start).count();
    double run_ms  = std::chrono::duration<double, std::milli>(t_run_end - t_run_start).count();
    size_t n_bars  = result.equity_curve.size();
    double bars_per_sec = n_bars / (run_ms / 1000.0);

    // 输出结果
    printf("\n========================================\n");
    printf("Strategy:       %s\n", a.strategy.c_str());
    printf("Interval:       %s\n", a.interval.c_str());
    printf("Initial cash:   $%.2f\n", result.initial_cash);
    printf("Final equity:   $%.2f\n", result.final_equity);
    printf("Total return:   %.2f%%\n", result.total_return * 100);
    printf("Annual return:  %.2f%%\n", result.annual_return * 100);
    printf("Sharpe ratio:   %.4f\n", result.sharpe);
    printf("Profit factor:  %.4f\n", result.profit_factor);
    printf("Max drawdown:   %.2f%%\n", result.max_drawdown * 100);
    printf("Total trades:   %u\n", result.total_trades);
    printf("Win rate:       %.2f%%\n", result.win_rate * 100);
    printf("Equity samples: %zu\n", n_bars);
    printf("----------------------------------------\n");
    printf("Data load:      %.0f ms\n", load_ms);
    printf("Engine run:     %.0f ms (%.2fM bars/s)\n", run_ms, bars_per_sec / 1e6);
    printf("========================================\n");
}

// ── list-data ──

static void cmd_list_data(const Args& a) {
    CHConfig cfg{a.ch_host, a.ch_port, a.ch_user, a.ch_password};

    auto rows = ch_query(cfg,
        "SELECT symbol, count() AS bars, "
        "min(open_time) AS first, max(open_time) AS last "
        "FROM qe.klines_1m GROUP BY symbol ORDER BY symbol");

    if (rows.empty()) {
        printf("No data in qe.klines_1m\n");
        return;
    }

    printf("%-15s %10s %22s %22s\n", "Symbol", "Bars", "First", "Last");
    printf("------------------------------------------------------------------------\n");
    for (auto& row : rows) {
        if (row.size() < 4) continue;
        printf("%-15s %10s %22s %22s\n",
               row[0].c_str(), row[1].c_str(), row[2].c_str(), row[3].c_str());
    }
}

// ── main ──

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);

    if (args.command == "backtest")
        cmd_backtest(args);
    else if (args.command == "list-data")
        cmd_list_data(args);
    else {
        fprintf(stderr, "Unknown command: %s\n", args.command.c_str());
        print_usage();
        return 1;
    }

    return 0;
}
