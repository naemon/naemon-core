Feature: Boolean object directives
	Boolean object directives use a leading 0 or 1.

	Trailing characters are accepted for compatibility but produce a warning.

    Scenario: register with trailing characters warns
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | trailing    | 127.0.0.1 | 0v       |
        And config verification pass
        Then the configuration warning should name register
        And the configuration warning should name 0v

    Scenario: register reading as true with trailing characters warns
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | tenish      | 127.0.0.1 | 10       |
        And config verification pass
        Then the configuration warning should name register

    Scenario: register 0 is accepted
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | unregistered| 127.0.0.1 | 0        |
        And config verification pass

    Scenario: register 1 is accepted
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | registered  | 127.0.0.1 | 1        |
        And config verification pass

    Scenario: another boolean directive warns too
        Given I have naemon host objects
            | use          | host_name   | address   | notifications_enabled |
            | default-host | notifyish   | 127.0.0.1 | 0v                    |
        And config verification pass
        Then the configuration warning should name notifications_enabled

    Scenario: the warning names register as written, not the internal field
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | named       | 127.0.0.1 | 0v       |
        And config verification pass
        Then the configuration warning should name register
        And the configuration warning should not name register_object

    Scenario: the warning names an aliased directive as written
        Given I have naemon host objects
            | use          | host_name   | address   | checks_enabled |
            | default-host | aliased     | 127.0.0.1 | 0v             |
        And config verification pass
        Then the configuration warning should name checks_enabled
        And the configuration warning should not name active_checks_enabled

    Scenario: the warning names obsess_over_host as written
        Given I have naemon host objects
            | use          | host_name   | address   | obsess_over_host |
            | default-host | obsessive   | 127.0.0.1 | 0v               |
        And config verification pass
        Then the configuration warning should name obsess_over_host
