#include <juce_core/juce_core.h>

#include <iostream>

// Usage: brEARk_Tests [part of a test name]   (no argument: all tests)
int main (int argc, char* argv[])
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    juce::Array<juce::UnitTest*> selected;

    for (auto* test : juce::UnitTest::getTestsInCategory ("brEARk"))
        if (argc < 2 || test->getName().containsIgnoreCase (argv[1]))
            selected.add (test);

    runner.runTests (selected);

    int passes = 0, failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        passes += runner.getResult (i)->passes;
        failures += runner.getResult (i)->failures;
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return (failures == 0 && passes > 0) ? 0 : 1;
}
