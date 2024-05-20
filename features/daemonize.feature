Feature: Daemonization
    Naemon should successfully daemonize if and only if there is a valid
    configuration available.

    Scenario: Naemon fails to daemonize when an invalid system config is
        provided
        Given I have an invalid naemon system configuration
        And config verification fail
        Then naemon should fail to start

    Scenario: Naemon fails to daemonize when an invalid object config is
        provided
        Given I have an invalid naemon object configuration
        And config verification fail
        Then naemon should fail to start

    Scenario: Naemon fails to daemonize when all config provided is invalid
        Given I have an invalid naemon object configuration
        And I have an invalid naemon system configuration
        And config verification fail
        Then naemon should fail to start

    Scenario: Naemon successfully daemonizes when a valid config is provided
        Given config verification pass
        Then naemon should successfully start
