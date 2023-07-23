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
    prn("client:", buf);
    co_return;
}

task<void> test_file_read() {
    prn(co_await async::slurp("/etc/hosts"));
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
    prn("Result:", ip);
    prn("Result2:", (co_await async::dns::ip_to_host(ip.c_str())).value_or("<not found>"));
    co_return;
}

task<void> coro_main() {
    co_await test_client();
    co_await test_file_read();
    co_await test_file_write();
    co_await test_file_rw();
    co_await test_dns();
    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto task = coro_main();
    while (poll_loop.has_tasks()) {
        prn("MAIN LOOP");
        poll_loop.think();
        prn("MAIN LOOP END");
    }
    rethrow_task(task);
    prn("main end");
    task.handle.destroy();
}
