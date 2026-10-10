#include <juce_core/juce_core.h>

#include <iostream>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("brEARk");

    int passes = 0, failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        passes += runner.getResult (i)->passes;
        failures += runner.getResult (i)->failures;
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return (failures == 0 && passes > 0) ? 0 : 1;
}
