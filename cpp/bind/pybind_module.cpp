#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "core/engine.h"
#include "core/sim_broker.h"
#include "core/strategy.h"
#include "data/csv_feed.h"
#include "data/tick.h"
#include "indicator/sma.h"
#include "indicator/ema.h"
#include "indicator/rsi.h"
#include "indicator/macd.h"
#include "indicator/bollinger.h"

namespace py = pybind11;

namespace qe {

// Strategy trampoline: 让 Python 子类可以 override C++ 虚函数
class PyStrategy : public Strategy {
public:
    using Strategy::Strategy;

    void on_init(Context& ctx) override {
        PYBIND11_OVERRIDE(void, Strategy, on_init, ctx);
    }

    void on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) override {
        PYBIND11_OVERRIDE_PURE(void, Strategy, on_bar, ctx, symbol_id, bar);
    }

    void on_stop(Context& ctx) override {
        PYBIND11_OVERRIDE(void, Strategy, on_stop, ctx);
    }

    void on_tick(Context& ctx, uint16_t symbol_id, const Trade& trade) override {
        PYBIND11_OVERRIDE(void, Strategy, on_tick, ctx, symbol_id, trade);
    }

    void on_order(Context& ctx, const Order& order) override {
        PYBIND11_OVERRIDE(void, Strategy, on_order, ctx, order);
    }
};

}  // namespace qe

PYBIND11_MODULE(qe, m) {
    m.doc() = "quant-engine: C++ backtest engine with Python strategy support";

    using namespace qe;

    // --- 枚举 ---
    py::enum_<Side>(m, "Side")
        .value("BUY", Side::BUY)
        .value("SELL", Side::SELL);

    py::enum_<OrderType>(m, "OrderType")
        .value("MARKET", OrderType::MARKET)
        .value("LIMIT", OrderType::LIMIT);

    py::enum_<OrderStatus>(m, "OrderStatus")
        .value("PENDING", OrderStatus::PENDING)
        .value("FILLED", OrderStatus::FILLED)
        .value("PARTIALLY_FILLED", OrderStatus::PARTIALLY_FILLED)
        .value("CANCELLED", OrderStatus::CANCELLED);

    // --- Bar ---
    py::class_<Bar>(m, "Bar")
        .def(py::init<>())
        .def_readwrite("timestamp_ms", &Bar::timestamp_ms)
        .def_readwrite("open", &Bar::open)
        .def_readwrite("high", &Bar::high)
        .def_readwrite("low", &Bar::low)
        .def_readwrite("close", &Bar::close)
        .def_readwrite("volume", &Bar::volume)
        .def_readwrite("quote_volume", &Bar::quote_volume);

    // --- Position ---
    py::class_<Position>(m, "Position")
        .def_readonly("quantity", &Position::quantity)
        .def_readonly("avg_entry_price", &Position::avg_entry_price)
        .def_readonly("unrealized_pnl", &Position::unrealized_pnl)
        .def_readonly("realized_pnl", &Position::realized_pnl);

    // --- FillEvent ---
    py::class_<FillEvent>(m, "FillEvent")
        .def_readonly("order_id", &FillEvent::order_id)
        .def_readonly("symbol_id", &FillEvent::symbol_id)
        .def_readonly("side", &FillEvent::side)
        .def_readonly("price", &FillEvent::price)
        .def_readonly("quantity", &FillEvent::quantity)
        .def_readonly("commission", &FillEvent::commission)
        .def_readonly("pnl", &FillEvent::pnl)
        .def_readonly("timestamp_ms", &FillEvent::timestamp_ms);

    // --- PerformanceResult ---
    py::class_<PerformanceResult>(m, "PerformanceResult")
        .def_readonly("sharpe", &PerformanceResult::sharpe)
        .def_readonly("max_drawdown", &PerformanceResult::max_drawdown)
        .def_readonly("annual_return", &PerformanceResult::annual_return)
        .def_readonly("total_return", &PerformanceResult::total_return)
        .def_readonly("total_trades", &PerformanceResult::total_trades)
        .def_readonly("win_rate", &PerformanceResult::win_rate)
        .def_readonly("initial_cash", &PerformanceResult::initial_cash)
        .def_readonly("final_equity", &PerformanceResult::final_equity)
        .def_readonly("equity_curve", &PerformanceResult::equity_curve);

    // --- Indicator 基类（只读接口） ---
    py::class_<Indicator>(m, "Indicator")
        .def("value", &Indicator::value)
        .def("ready", &Indicator::ready);

    // --- SMA ---
    py::class_<SMA, Indicator>(m, "SMA")
        .def(py::init<int>(), py::arg("period"))
        .def("period", &SMA::period);

    // --- EMA ---
    py::class_<EMA, Indicator>(m, "EMA")
        .def(py::init<int>(), py::arg("period"))
        .def("period", &EMA::period);

    // --- RSI ---
    py::class_<RSI, Indicator>(m, "RSI")
        .def(py::init<int>(), py::arg("period") = 14)
        .def("period", &RSI::period);

    // --- MACD ---
    py::class_<MACD, Indicator>(m, "MACD")
        .def(py::init<int, int, int>(),
             py::arg("fast_period") = 12,
             py::arg("slow_period") = 26,
             py::arg("signal_period") = 9)
        .def("signal", &MACD::signal)
        .def("histogram", &MACD::histogram);

    // --- Bollinger ---
    py::class_<Bollinger, Indicator>(m, "Bollinger")
        .def(py::init<int, double>(),
             py::arg("period") = 20,
             py::arg("num_std") = 2.0)
        .def("upper", &Bollinger::upper)
        .def("lower", &Bollinger::lower)
        .def("bandwidth", &Bollinger::bandwidth);

    // --- Order ---
    py::class_<Order>(m, "Order")
        .def_readonly("id", &Order::id)
        .def_readonly("symbol_id", &Order::symbol_id)
        .def_readonly("side", &Order::side)
        .def_readonly("type", &Order::type)
        .def_readonly("price", &Order::price)
        .def_readonly("quantity", &Order::quantity)
        .def_readonly("filled_quantity", &Order::filled_quantity)
        .def_readonly("commission", &Order::commission)
        .def_readonly("status", &Order::status)
        .def_readonly("created_at", &Order::created_at)
        .def_readonly("filled_at", &Order::filled_at);

    // --- Trade ---
    py::class_<Trade>(m, "Trade")
        .def(py::init<>())
        .def_readwrite("timestamp_ms", &Trade::timestamp_ms)
        .def_readwrite("price", &Trade::price)
        .def_readwrite("quantity", &Trade::quantity)
        .def_readwrite("is_buyer_maker", &Trade::is_buyer_maker);

    // --- Context ---
    // 模板 indicator<T> 需要逐个实例化
    py::class_<Context>(m, "Context")
        .def("buy", &Context::buy)
        .def("sell", &Context::sell)
        .def("buy_limit", &Context::buy_limit)
        .def("sell_limit", &Context::sell_limit)
        .def("cancel", &Context::cancel)
        .def("position", &Context::position, py::return_value_policy::reference)
        .def("equity", &Context::equity)
        .def("cash", &Context::cash)
        .def("symbol", &Context::symbol)
        .def("symbol_name", &Context::symbol_name, py::return_value_policy::reference)
        .def("current_time", &Context::current_time)
        // 显式实例化每个指标类型的 indicator<T>()
        .def("sma", &Context::indicator<SMA, int>,
             py::arg("symbol_id"), py::arg("period"),
             py::return_value_policy::reference)
        .def("ema", &Context::indicator<EMA, int>,
             py::arg("symbol_id"), py::arg("period"),
             py::return_value_policy::reference)
        .def("rsi", &Context::indicator<RSI, int>,
             py::arg("symbol_id"), py::arg("period") = 14,
             py::return_value_policy::reference)
        .def("macd", [](Context& ctx, uint16_t sid, int fast, int slow, int signal) -> MACD& {
                 return ctx.indicator<MACD>(sid, fast, slow, signal);
             },
             py::arg("symbol_id"),
             py::arg("fast_period") = 12,
             py::arg("slow_period") = 26,
             py::arg("signal_period") = 9,
             py::return_value_policy::reference)
        .def("bollinger", [](Context& ctx, uint16_t sid, int period, double num_std) -> Bollinger& {
                 return ctx.indicator<Bollinger>(sid, period, num_std);
             },
             py::arg("symbol_id"),
             py::arg("period") = 20,
             py::arg("num_std") = 2.0,
             py::return_value_policy::reference);

    // --- Strategy（可被 Python 继承） ---
    py::class_<Strategy, PyStrategy, std::shared_ptr<Strategy>>(m, "Strategy")
        .def(py::init<>())
        .def("on_init", &Strategy::on_init)
        .def("on_bar", &Strategy::on_bar)
        .def("on_tick", &Strategy::on_tick)
        .def("on_order", &Strategy::on_order)
        .def("on_stop", &Strategy::on_stop);

    // --- SimBrokerConfig ---
    py::class_<SimBrokerConfig>(m, "SimBrokerConfig")
        .def(py::init<>())
        .def_readwrite("cash", &SimBrokerConfig::cash)
        .def_readwrite("commission_rate", &SimBrokerConfig::commission_rate)
        .def_readwrite("slippage", &SimBrokerConfig::slippage);

    // --- CsvFeed ---
    py::class_<CsvFeed>(m, "CsvFeed")
        .def(py::init<uint16_t, const std::string&>(),
             py::arg("symbol_id"), py::arg("csv_path"))
        .def(py::init<uint16_t, std::vector<Bar>>(),
             py::arg("symbol_id"), py::arg("bars"))
        .def("size", &CsvFeed::size);

    // --- Engine ---
    py::class_<Engine>(m, "Engine")
        .def(py::init<>())
        .def("add_feed", [](Engine& e, uint16_t sid, const std::string& path) {
            e.add_feed(std::make_unique<CsvFeed>(sid, path));
        }, py::arg("symbol_id"), py::arg("csv_path"))
        .def("add_feed_bars", [](Engine& e, uint16_t sid, std::vector<Bar> bars) {
            e.add_feed(std::make_unique<CsvFeed>(sid, std::move(bars)));
        }, py::arg("symbol_id"), py::arg("bars"))
        .def("set_broker", [](Engine& e, const SimBrokerConfig& cfg) {
            e.set_broker(std::make_unique<SimBroker>(cfg));
        }, py::arg("config"))
        .def("add_strategy", [](Engine& e, std::shared_ptr<Strategy> s) {
            e.add_strategy(std::move(s));
        }, py::arg("strategy"))
        .def("symbol_id", [](Engine& e, const std::string& name) {
            return e.symbols().id(name);
        }, py::arg("name"))
        .def("run", &Engine::run);
}
