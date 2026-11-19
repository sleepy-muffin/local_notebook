// Opening the same assistant twice must be a refusal, not a crash: the second
// `client` cannot take the instance lock, so its construction throws
// `open_error` carrying `ERR_ASSISTANT_LOCK_FAILED` and no object is left behind.
// The GUI relies on this to show a dialog and go back to the picker instead of
// vanishing.
//
//   reopen-conflict-check [data_root]
//
// The root defaults to a throwaway directory under /tmp.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "client.hpp"
#include "store.hpp"

using namespace felis::bell;

static int failures = 0;

static void check(bool ok, const char* what) {
  std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

int main(int argc, char** argv) {
  const std::filesystem::path root = argc > 1
      ? std::filesystem::path(argv[1])
      : std::filesystem::path("/tmp/local-notebook-reopen-conflict");

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  const std::string uuid = "reopen-conflict";

  {
    store seed(root);
    assistant ass;
    ass.name_ = "Reopen Conflict";
    seed.save_assistant(uuid, ass);
  }

  // The first client holds the lock for as long as it is alive.
  std::unique_ptr<client> first;
  try {
    first = std::make_unique<client>(store(root), uuid);
  } catch (const std::exception& error) {
    std::printf("first open threw: %s\n", error.what());
  }

  check(first != nullptr, "first open takes the lock");

  // The second one must refuse by throwing `open_error`, never by SIGSEGV.
  bool wrong_exception = false;
  bool refused = false;

  try {
    const client second(store(root), uuid);
  } catch (const open_error& error) {
    refused = error.code() == error_code::ERR_ASSISTANT_LOCK_FAILED;
  } catch (const std::exception&) {
    wrong_exception = true;
  }

  check(!wrong_exception, "second open throws only open_error");
  check(refused, "second open reports ERR_ASSISTANT_LOCK_FAILED");

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED",
              failures);
  return failures == 0 ? 0 : 1;
}
