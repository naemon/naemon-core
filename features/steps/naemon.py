from behave import given, when, then, use_step_matcher
import subprocess
import os.path
import signal
import time

use_step_matcher('re')


@given('I have naemon (?P<object_type>.+) objects')
def add_naemon_object(context, object_type):
    print ("Object type %s" % object_type)
    for row in context.table:
        context.naemonobjconfig.add_obj(object_type, context.table)


@given('I have naemon config (?P<parameter>.+) set to (?P<value>.+)')
def set_naemon_parameter(context, parameter, value):
    context.naemonsysconfig.set_var(parameter, value)


@given('I have an invalid naemon system configuration')
def invalid_sysconfig(context):
    context.execute_steps(u'Given I have naemon config invalid_param set to x')


@given('I have an invalid naemon object configuration')
def invalid_objconfig(context):
    context.execute_steps(u'''
        Given I have naemon host objects
            | use          | host_name    | address   | check_command |
            | default-host | invalid_host | 127.0.0.1 | non_existing  |
    ''')


@given('I write config to file')
def configuration_to_file(context):
    context.naemonobjconfig.to_file(context.naemonobjconfig.filename)
    context.naemonsysconfig.to_file(context.naemonsysconfig.filename)
    context.execute_steps(u'Given I have directory checkresults')


@given('I verify the naemon configuration')
def config_verification(context):
    context.execute_steps(u'Given I write config to file')
    args = [context.naemon_exec_path, '--allow-root', '--verify-config',
            context.naemonsysconfig.filename]
    context.return_code = subprocess.call(args)


@given('config verification fail')
def config_verification_fail(context):
    context.execute_steps(u'Given I verify the naemon configuration')
    assert context.return_code != 0, (
        'Return code was %s' % context.return_code
    )


@given('config verification pass')
def config_verification_pass(context):
    context.execute_steps(u'Given I verify the naemon configuration')
    assert context.return_code == 0, (
        'Return code was not 0 (got %s)' % context.return_code
    )


@given('I start naemon')
@when('I start naemon')
def naemon_start(context):
    context.execute_steps(u'Given I write config to file')
    args = [context.naemon_exec_path, '--allow-root', '--daemon',
            context.naemonsysconfig.filename]
    context.return_code = subprocess.call(args)


@when('I restart naemon')
def naemon_restart(context):
    assert os.path.exists('./naemon.pid'), (
        'Naemon pid file was not found'
    )
    pid = int(open('./naemon.pid').read())
    try:
        os.kill(pid, signal.SIGHUP)
        print ('Sent SIGHUP to naemon process (%i)' % pid)
    except OSError as e:
        print (os.strerror(e.errno))
        pass


@then('naemon should fail to start')
def naemon_start_fail(context):
    context.execute_steps(u'When I start naemon')
    assert context.return_code != 0, (
        'Return code was %s' % context.return_code
    )


@then('naemon should successfully start')
def naemon_start_success(context):
    context.execute_steps(u'When I start naemon')
    assert context.return_code == 0, (
        'Return code was not 0 (got %s)' % context.return_code
    )


@then('naemon should be running')
def naemon_is_running(context):
    assert os.path.exists('./naemon.pid'), (
        'Naemon pid file was not found'
    )
    pid = int(open('./naemon.pid').read())
    is_running = True
    try:
        os.kill(pid, 0)
    except OSError:
        is_running = False
    assert is_running is True, (
        'Naemon is not running!'
    )


@when('I wait for (?P<seconds>[0-9]+) seconds?')
def asdf(context, seconds):
    print ('Waiting for %s seconds' % seconds)
    time.sleep(int(seconds))
