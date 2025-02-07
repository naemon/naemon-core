from behave import given, when, then, use_step_matcher
import subprocess
import os
import os.path
import signal
import time

from support import slurp_file

use_step_matcher('re')


@given('I have naemon (?P<object_type>.+) objects')
def add_naemon_object(context, object_type):
    print("Object type %s" % object_type)
    for row in context.table:
        context.naemonobjconfig.add_obj(object_type, context.table)


@given('I have naemon config (?P<parameter>.+) set to (?P<value>.+)')
def set_naemon_parameter(context, parameter, value):
    context.naemonsysconfig.set_var(parameter, value)


@given('I have an invalid naemon system configuration')
def invalid_sysconfig(context):
    context.execute_steps('Given I have naemon config invalid_param set to x')


@given('I have an invalid naemon object configuration')
def invalid_objconfig(context):
    context.execute_steps('''
        Given I have naemon host objects
            | use          | host_name    | address   | check_command |
            | default-host | invalid_host | 127.0.0.1 | non_existing  |
    ''')


@given('I have an invalid naemon contact configuration')
def invalid_contactconfig(context):
    context.execute_steps('''
        Given I have naemon contact objects
            | use             | contact_name  | host_notification_commands |
            | default-contact | invalid       | invalid_cmd                |
    ''')


@given('I write config to file')
def configuration_to_file(context):
    context.naemonobjconfig.to_file(context.naemonobjconfig.filename)
    context.naemonsysconfig.to_file(context.naemonsysconfig.filename)
    context.execute_steps('Given I have directory checkresults')


@given('I verify the naemon configuration')
def config_verification(context):
    context.execute_steps('Given I write config to file')
    args = [context.naemon_exec_path, '--allow-root', '--verify-config',
            context.naemonsysconfig.filename]
    context.return_code = subprocess.call(args)


@given('config verification fail')
def config_verification_fail(context):
    context.execute_steps('Given I verify the naemon configuration')
    assert context.return_code != 0, (
        'Return code was %s' % context.return_code
    )


@given('config verification pass')
def config_verification_pass(context):
    context.execute_steps('Given I verify the naemon configuration')
    assert context.return_code == 0, (
        'Return code was not 0 (got %s)' % context.return_code
    )


@given('I start naemon')
@when('I start naemon')
def naemon_start(context):
    context.execute_steps('Given I write config to file')
    args = [context.naemon_exec_path, '--allow-root', '--daemon',
            context.naemonsysconfig.filename]
    context.return_code = subprocess.call(args)


@given('I start naemon and wait until it is ready')
def naemon_start_and_wait_ready(context):
    context.execute_steps('Given I start naemon')
    context.execute_steps('Given I wait 10 seconds for naemon to be ready')


@when('I restart naemon')
def naemon_restart(context):
    assert os.path.exists('./naemon.pid'), (
        'Naemon pid file was not found'
    )
    pid = int(slurp_file('naemon.pid'))
    try:
        os.kill(pid, signal.SIGHUP)
        print(('Sent SIGHUP to naemon process (%i)' % pid))
    except OSError as e:
        print((os.strerror(e.errno)))
        pass


@when('I stop naemon')
def naemon_stop(context):
    assert os.path.exists('./naemon.pid'), (
        'Naemon pid file was not found'
    )
    pid = int(slurp_file('naemon.pid'))
    try:
        os.kill(pid, signal.SIGTERM)
        print('Sent SIGTERM to naemon process (%i)' % pid)
    except OSError as e:
        print(e.strerror)


@then('naemon should fail to start')
def naemon_start_fail(context):
    context.execute_steps('When I start naemon')
    assert context.return_code != 0, (
        'Return code was %s' % context.return_code
    )


@then('naemon should successfully start')
def naemon_start_success(context):
    context.execute_steps('When I start naemon')
    assert context.return_code == 0, (
        'Return code was not 0 (got %s)' % context.return_code
    )


@given(r'I wait (?:(?P<timeout_s>\d+) seconds? )?for naemon to be ready')
def naemon_started_and_ready(context, timeout_s):
    timeout_s = int(timeout_s or 30)
    timeout = time.monotonic() + timeout_s
    ready = False
    while not ready and time.monotonic() < timeout:
        try:
            log = slurp_file('naemon.log')
            # When we see this line in the log, we'll wait 1 more second and
            # then Naemon should be ready, with signal handlers setup so that a
            # test can SIGTERM it.
            if 'Naemon successfully initialized' in log:
                ready = True
                time.sleep(1)
                break
        except OSError:
            pass
        time.sleep(1)
    assert ready, "Naemon did not start within %d seconds" % timeout_s


@then('naemon should be running')
def naemon_is_running(context):
    assert os.path.exists('./naemon.pid'), (
        'Naemon pid file was not found'
    )
    pid = int(slurp_file('naemon.pid'))
    is_running = True
    try:
        os.kill(pid, 0)
    except OSError:
        is_running = False
    assert is_running is True, (
        'Naemon is not running!'
    )


@then('naemon should not be running')
def naemon_is_not_running(context):
    assert not os.path.exists('./naemon.pid'), (
        'Naemon is running'
    )


@then('naemon should output a sensible error message')
def naemon_error_msg(context):
    context.execute_steps('When I start naemon')
    assert 'Error: Host notification command' in slurp_file('naemon.log')


@then('naemon should output a retention data saved log message')
def naemon_retention_msg(context):
    assert 'Retention data successfully saved.' in slurp_file('naemon.log')


@when('I wait for (?P<seconds>[0-9]+) seconds?')
def asdf(context, seconds):
    print('Waiting for %s seconds' % seconds)
    time.sleep(int(seconds))
