class NaemonSystemConfig(object):

    filename = 'naemon.cfg'

    def __init__(self):
        self.current_config = {
            'query_socket': 'naemon.qh',
            'check_result_path': 'checkresults',
            'event_broker_options': '-1',
            'command_file': 'naemon.cmd',
            'object_cache_file': 'objects.cache',
            'status_file': 'status_file.sav',
            'log_file': 'naemon.log',
            'retain_state_information': '1',
            'state_retention_file': 'status.sav',
            'execute_host_checks': '0',
            'execute_service_checks': '0',
            'cfg_file': 'oconf.cfg',
            'lock_file': 'naemon.pid'
        }

    def set_var(self, name, value):
        self.current_config[name] = value

    def to_string(self):
        res = ''
        for key, value in self.current_config.items():
            res += ("%s=%s\n" % (key, value))
        return res

    def to_file(self, filename):
        f = open(filename, 'w')
        f.write(self.to_string())
        f.close()
