#include "process.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace std::chrono_literals;

static int failures = 0;

static void check(bool ok, const char* what) {
  std::printf("%-48s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

int main() {
  {
    felis::process::spec s;
    s.command = "printf 'hello'";
    auto r = felis::process::run(s);
    check(r.outcome == felis::process::status::exited && r.exit_code == 0 &&
              r.output == "hello",
          "basic stdout + exit 0");
  }
  {
    felis::process::spec s;
    s.command = "echo out; echo err >&2; exit 3";
    auto r = felis::process::run(s);
    check(r.exit_code == 3 && r.output.find("out") != std::string::npos &&
              r.output.find("err") != std::string::npos,
          "merged stderr + exit code");
  }
  {
    felis::process::spec s;
    s.command = "seq 1 200000";
    s.max_output_bytes = 1000;
    auto r = felis::process::run(s);
    check(r.truncated && r.output.size() < 1200 &&
              r.output.find("truncated") != std::string::npos &&
              r.output.find("200000") != std::string::npos &&
              r.output.find("1\n") == 0,
          "head+tail truncation keeps first and last");
  }
  {
    felis::process::spec s;
    s.command = "sleep 30";
    s.timeout = 300ms;
    auto r = felis::process::run(s);
    check(r.outcome == felis::process::status::killed && r.timed_out,
          "timeout kills the process");
  }
  {
    felis::process::spec s;
    s.command = "sleep 30";
    std::stop_source source;
    s.stop = source.get_token();
    std::thread canceller([&] {
      std::this_thread::sleep_for(200ms);
      source.request_stop();
    });
    auto r = felis::process::run(s);
    canceller.join();
    check(r.outcome == felis::process::status::killed && r.cancelled,
          "stop request kills the process");
  }
  {
    felis::process::spec s;
    s.command = "pwd";
    s.cwd = "/tmp";
    auto r = felis::process::run(s);
    check(r.exit_code == 0 && r.output.find("/tmp") != std::string::npos,
          "working directory honoured");
  }
  {
    ::setenv("SECRET_TEST_TOKEN", "s3cr3t", 1);
    ::setenv("HARMLESS_VALUE", "fine", 1);
    felis::process::spec s;
    s.command = "echo tok=[$SECRET_TEST_TOKEN] keep=[$HARMLESS_VALUE]";
    auto r = felis::process::run(s);
    check(r.output.find("s3cr3t") == std::string::npos &&
              r.output.find("fine") != std::string::npos,
          "credential-looking env vars stripped");
  }
  {
    // Six 3-byte characters, capped at 8 bytes: the head ends mid-character and
    // the tail begins mid-character, so both sides must be trimmed.
    felis::process::spec s;
    s.command = "printf '世界世界世界'";
    s.max_output_bytes = 8;
    auto r = felis::process::run(s);
    check(r.output == "世\n… 12 bytes truncated …\n界", "no broken utf-8 at cut");
  }
  {
    felis::process::spec s;
    s.command = "exit 42";
    auto r = felis::process::run(s);
    check(r.exit_code == 42, "nonzero exit code");
  }
  {
    felis::process::spec s;
    s.command = "kill -TERM $$";
    auto r = felis::process::run(s);
    check(r.exit_code == -SIGTERM, "signal death reported as negative");
  }

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED",
              failures);
  return failures == 0 ? 0 : 1;
}
