Feature: Logging
	Checks that various log messages are printed correctly

    Scenario: Naemon fails to daemonize when an invalid
        host_notification_commands argument is provided in a contact config
        Given I have an invalid naemon contact configuration
        And config verification fail
        Then naemon should output a sensible error message

    Scenario: Output retention data log message when stopped
        Given I start naemon and wait until it is ready
        When I stop naemon
        And I wait for 5 seconds
        Then naemon should not be running
        And naemon should output a retention data saved log message
