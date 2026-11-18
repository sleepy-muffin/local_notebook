// Opening the same assistant twice must be a refusal, not a crash: the second
// `client` cannot take the instance lock, so it has to report that through
// `open_assistant()` and leave itself unusable but harmless. The GUI relies on
// this to show a dialog and go back to the picker instead of vanishing.
//
//   reopen-conflict-check [data_root]
//
// The root defaults to a throwaway directory under /tmp.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
  store first_store(root);
  client first(std::move(first_store), uuid);
  check(first.open_assistant() == error_code::SUCCESS, "first open takes the lock");

  // The second one must refuse without throwing.
  bool threw = false;
  bool refused = false;

  try {
    store second_store(root);
    client second(std::move(second_store), uuid);

    refused = second.open_assistant() == error_code::ERR_ASSISTANT_LOCK_FAILED;
  } catch (const std::exception&) {
    threw = true;
  }

  check(!threw, "second open does not throw");
  check(refused, "second open reports ERR_ASSISTANT_LOCK_FAILED");

  std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED",
              failures);
  return failures == 0 ? 0 : 1;
}
