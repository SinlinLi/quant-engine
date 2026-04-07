#pragma once
// 极简 ClickHouse HTTP 客户端（POSIX socket，零外部依赖）

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

namespace qe {

struct CHConfig {
    std::string host = "localhost";
    int port = 8123;
    std::string user = "default";
    std::string password;  // 通过环境变量 QE_CH_PASSWORD 或命令行 --ch-password 设置
};

// 每行是一个 vector<string>（TSV 列）
using CHRows = std::vector<std::vector<std::string>>;

inline std::string url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

inline std::string ch_http_request(const CHConfig& cfg, const std::string& query) {
    // 建立 TCP 连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) throw std::runtime_error("socket() failed");

    struct hostent* he = gethostbyname(cfg.host.c_str());
    if (!he) { close(sock); throw std::runtime_error("DNS lookup failed: " + cfg.host); }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        throw std::runtime_error("connect() failed");
    }

    // 构建 HTTP GET 请求（query 作为 URL 参数）
    std::string path = "/?query=" + url_encode(query)
        + "&user=" + url_encode(cfg.user)
        + "&password=" + url_encode(cfg.password);

    std::string req = "GET " + path + " HTTP/1.0\r\n"
        "Host: " + cfg.host + "\r\n"
        "Connection: close\r\n\r\n";

    // 发送
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t n = write(sock, req.data() + sent, req.size() - sent);
        if (n <= 0) { close(sock); throw std::runtime_error("write() failed"); }
        sent += n;
    }

    // 接收
    std::string response;
    char buf[65536];
    while (true) {
        ssize_t n = read(sock, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, n);
    }
    close(sock);

    // 解析 HTTP 响应：分离 header 和 body
    auto hdr_end = response.find("\r\n\r\n");
    if (hdr_end == std::string::npos)
        throw std::runtime_error("malformed HTTP response");

    std::string header = response.substr(0, hdr_end);
    std::string body = response.substr(hdr_end + 4);

    // 检查状态码
    if (header.find("200 OK") == std::string::npos) {
        // body 就是 ClickHouse 错误信息
        throw std::runtime_error("ClickHouse error: " + body.substr(0, 500));
    }

    return body;
}

inline CHRows ch_query(const CHConfig& cfg, const std::string& query) {
    std::string body = ch_http_request(cfg, query);

    CHRows rows;
    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line == "\r") continue;
        // 去除末尾 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 按 Tab 分割
        std::vector<std::string> cols;
        std::istringstream ls(line);
        std::string col;
        while (std::getline(ls, col, '\t'))
            cols.push_back(col);
        if (!cols.empty())
            rows.push_back(std::move(cols));
    }
    return rows;
}

}  // namespace qe
