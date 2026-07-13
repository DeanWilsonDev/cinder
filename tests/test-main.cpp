#include <cimmerian/test.hpp>

#include <firefly/log-registry.hpp>
#include <firefly/log.hpp>

int main()
{
  // CinderMixer/AudioBackend log via Firefly as part of normal operation
  // (stem registration, fades, effects); Firefly throws if a message is
  // logged before any logger is registered, so this must happen before any
  // test constructs a CinderMixer. Console-only -- tests never touch disk.
  Firefly::LogRegistry::RegisterLogger(FIREFLY_DEFAULT_LOGGER);

  Cimmerian::TestRunner runner;
  Cimmerian::TestRunSummary summary = runner.RunAll(&Cimmerian::TestRegistry::GetInstance());
  return summary.failed > 0 ? 1 : 0;
}
