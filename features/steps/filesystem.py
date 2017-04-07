from behave import given, use_step_matcher
import os.path

use_step_matcher('re')


@given('I have directory (?P<directory>.+)')
def create_directory(context, directory):
    if not os.path.exists(directory):
        os.makedirs(directory)
    assert os.path.exists(directory) is not False, (
        'Failed to create directory (%s)' % directory
    )
