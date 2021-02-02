from support.naemon_object_config import NaemonObjectConfig
from support.naemon_system_config import NaemonSystemConfig
from support import slurp_file
import tempfile
import os
import os.path
import signal
import shutil


def before_all(context):
    # check if we have set relative paths in behave.ini
    if 'shared_libs_path' in context.config.userdata:
        os.environ['LD_LIBRARY_PATH'] = \
            os.path.abspath(context.config.userdata['shared_libs_path'])

    # use naemon executable defined in PATH if nothing else is given
    context.naemon_exec_path = 'naemon'
    if 'naemon_exec_path' in context.config.userdata:
        context.naemon_exec_path = \
            os.path.abspath(context.config.userdata['naemon_exec_path'])


def before_scenario(context, scenario):
    context.wrkdir = os.getcwd()
    context.tmpdir = tempfile.mkdtemp()
    assert os.path.exists(context.tmpdir) is not False, (
        'Failed to create a temporary directory'
    )
    print('Changing directory to %s from %s' % (
            context.tmpdir, context.wrkdir
        )
    )
    os.chdir(context.tmpdir)
    context.naemonsysconfig = NaemonSystemConfig()
    context.naemonobjconfig = NaemonObjectConfig()


def after_scenario(context, scenario):
    # Kill any naemon daemonsv
    if os.path.isfile('naemon.pid'):
        pid = int(slurp_file('naemon.pid'))
        try:
            os.kill(pid, signal.SIGTERM)
            print('Killed the naemon process (%i)' % pid)
        except OSError as e:
            print(e.strerror)
    print('Changing directory to %s from %s' % (
        context.wrkdir, context.tmpdir
        )
    )
    os.chdir(context.wrkdir)
    if scenario.status != 'failed':
        print('Deleting temporary directory %s' % context.tmpdir)
        shutil.rmtree(context.tmpdir, True)
