Feature: Boolean object directives
	Boolean object directives accept only the exact values 0 and 1.

	The parser used to inspect the first character of the value only, so a
	value such as "0v" was silently read as 0. For "register" that quietly
	turned an object into an unregistered template, removing it from
	monitoring with no warning at all.

    Scenario: register with trailing characters is rejected
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | trailing    | 127.0.0.1 | 0v       |
        And config verification fail

    Scenario: register reading as true with trailing characters is rejected
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | tenish      | 127.0.0.1 | 10       |
        And config verification fail

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

    Scenario: a boolean directive other than register is validated too
        Given I have naemon host objects
            | use          | host_name   | address   | notifications_enabled |
            | default-host | notifyish   | 127.0.0.1 | 0v                    |
        And config verification fail

    Scenario: the error names register as written, not the internal field
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | named       | 127.0.0.1 | 0v       |
        And config verification fail
        Then the configuration error should name register
        And the configuration error should not name register_object

    Scenario: the error quotes the rejected value
        Given I have naemon host objects
            | use          | host_name   | address   | register |
            | default-host | quoted      | 127.0.0.1 | 0v       |
        And config verification fail
        Then the configuration error should name 0v

    Scenario: the error names an aliased directive as written
        Given I have naemon host objects
            | use          | host_name   | address   | checks_enabled |
            | default-host | aliased     | 127.0.0.1 | 0v             |
        And config verification fail
        Then the configuration error should name checks_enabled
        And the configuration error should not name active_checks_enabled

    Scenario: the error names obsess_over_host as written
        Given I have naemon host objects
            | use          | host_name   | address   | obsess_over_host |
            | default-host | obsessive   | 127.0.0.1 | 0v               |
        And config verification fail
        Then the configuration error should name obsess_over_host
