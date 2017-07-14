Feature: Restart
    Naemon can be restarted without killing the process

    Scenario: Naemon is restarted when sending SIGHUP to process
        Given I start naemon
        When I restart naemon
        And I wait for 10 seconds
        Then naemon should be running
