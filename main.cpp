#include <dbg.h>
#include <fmt.h>
#include <ex.h>

#include "async/dns.h"
#include "async/tcp.h"
#include "async/udp.h"
#include "async/file.h"

#include "async/poll_loop.h"
#include <signal.h>


task<void> test_client() {
    async::stream stream = co_await async::connect("93.184.216.34", 80);
    prn("client connected.");
    co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:close\r\n\r\n");
    auto buf = co_await stream.read_until("\r\n\r\n");
    prn("client done.");
    co_return;
}

task<void> test_file_read() {
    co_await async::slurp("/etc/hosts");
    prn("file_read done.");
}

task<void> test_file_write() {
    async::stream stream = co_await async::open_write("foo", false);
    co_await stream.write("bar\n");
    co_await stream.close();
    assert(co_await async::slurp("foo") == "bar\n");
}

task<void> test_file_rw() {
    async::stream stream = co_await async::open_rw("foo", "bar", false);
    co_await stream.write("baz\n");
    assert(co_await stream.read_until_eof() == "bar\n");
    co_await stream.close();
    assert(co_await async::slurp("bar") == "baz\n");
}

task<void> test_dns() {
    std::string ip = co_await async::dns::host_to_ip("pie.dev");
    prn("dns:", ip);
    prn("dns reverse:", (co_await async::dns::ip_to_host(ip.c_str())).value_or("<not found>"));
    co_return;
}

task<void> test_gather() {
    co_await gather_void(
        test_client(),
        test_file_read(),
        // test_file_write(),
        // test_file_rw(),
        test_dns()
    );
    // prn("Gathered", x, y);
}

task<void> coro_main() {
    co_await test_gather();
    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto task = coro_main();
    while (poll_loop.has_tasks()) {
        prn(">>>");
        poll_loop.think();
        prn(">>>\n");
    }
    rethrow_task(task);
    prn("main end");
    task.handle.destroy();
    task.was_awaited = true;
}
