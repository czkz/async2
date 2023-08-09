#include <dbg.h>
#include <fmt.h>
#include <ex.h>

#include "async/dns.h"
#include "async/tcp.h"
#include "async/udp.h"
#include "async/file.h"
#include "async/tls.h"
#include "async/slurp.h"

#include "async/poll_loop.h"
#include <signal.h>


async::task<void> test_client() {
    async::stream stream = co_await async::tcp::connect("93.184.216.34", 80);
    prn(__FUNCTION__, "connected.");
    co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:close\r\n\r\n");
    auto buf = co_await stream.read_until("\r\n\r\n");
    prn(__FUNCTION__, "done.");
    co_return;
}

async::task<void> test_file_read() {
    prn(__FUNCTION__, "start.");
    async::stream stream = co_await async::file::open_read("/etc/hosts");
    co_await stream.read_until_eof();
    prn(__FUNCTION__, "done.");
}

async::task<void> test_file_write() {
    prn(__FUNCTION__, "start.");
    async::stream stream = co_await async::file::open_write("foo", false);
    co_await stream.write("bar\n");
    co_await stream.close();
    assert(co_await async::slurp("foo") == "bar\n");
    prn(__FUNCTION__, "done.");
}

async::task<void> test_file_rw() {
    prn(__FUNCTION__, "start.");
    async::stream stream = co_await async::file::open_rw("foo", "bar", false);
    co_await stream.write("baz\n");
    assert(co_await stream.read_until_eof() == "bar\n");
    co_await stream.close();
    assert(co_await async::slurp("bar") == "baz\n");
    prn(__FUNCTION__, "done.");
}

async::task<void> test_dns() {
    prn(__FUNCTION__, "start.");
    std::string ip = co_await async::dns::host_to_ip("pie.dev");
    prn("dns:", ip);
    prn("dns reverse:", (co_await async::dns::ip_to_host(ip)).value_or("<not found>"));
    prn(__FUNCTION__, "done.");
}

async::task<void> test_tls() {
    prn(__FUNCTION__, "start.");
    async::stream stream = co_await async::tls::connect("example.com", 443);
    prn("tls connected.");
    co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:close\r\n\r\n");
    // co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:keep-alive\r\n\r\n");
    prn("tls sent.");
    auto buf = co_await stream.read_until("\r\n\r\n");
    prn(__FUNCTION__, "done.");
}

async::task<void> test_slurp() {
    prn(__FUNCTION__, "start.");
    auto hosts1 = co_await async::slurp("/etc/hosts");
    auto hosts2 = co_await async::slurp("file:///etc/hosts");
    assert(hosts1 == hosts2);
    // http://duck.com -> https://duck.com -> https://duckduckgo.com
    auto html = co_await async::slurp("http://duck.com");
    assert(html.ends_with("</html>\n"));
    prn(__FUNCTION__, "done.");
}

async::task<void> test_gather() {
    co_await gather_void(
        test_client(),
        test_file_read(),
        // test_file_write(),
        // test_file_rw(),
        test_dns(),
        test_tls(),
        test_slurp()
    );
    // prn("Gathered", x, y);
}

async::task<void> coro_main() {
    co_await test_gather();
    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto task = coro_main();
    while (poll_loop.has_tasks()) {
        // prn(">>>");
        poll_loop.think();
        // prn(">>>\n");
    }
    rethrow_task(task);
    prn("main end");
    task.handle.destroy();
    task.was_awaited = true;
}
