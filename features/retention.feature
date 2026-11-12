Feature: Retention data
    Retention data that cannot be loaded completely

    Scenario: Naemon refuses to start when the retention file cannot be read
        Given the retention file cannot be read
        Then naemon should fail to start
        And the naemon log should contain the retention data could not be loaded completely
        And the retention file should still be there
