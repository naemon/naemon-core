class NaemonObjectConfig(object):

    filename = 'oconf.cfg'

    def __init__(self):
        self.current_config = {
            'contact': [{
                'name': 'default-contact',
                'contact_name': 'default-contact',
                'host_notifications_enabled': '1',
                'service_notifications_enabled': '1',
                'host_notification_period': '24x7',
                'service_notification_period': '24x7',
                'host_notification_options': 'a',
                'service_notification_options': 'a',
                'host_notification_commands': 'dummy_command',
                'service_notification_commands': 'dummy_command',
            }],
            'timeperiod': [{
                'timeperiod_name': '24x7',
                'alias': '24x7',
                'sunday': '00:00-24:00',
                'monday': '00:00-24:00',
                'tuesday': '00:00-24:00',
                'wednesday': '00:00-24:00',
                'thursday': '00:00-24:00',
                'friday': '00:00-24:00',
                'saturday': '00:00-24:00'
            }],
            'command': [{
                'command_name': 'dummy_command',
                'command_line': 'exit 0'
            }],
            'host': [{
                'check_command': 'dummy_command',
                'max_check_attempts': 3,
                'check_interval': 5,
                'retry_interval': 0,
                'obsess': 0,
                'check_freshness': 0,
                'active_checks_enabled': 0,
                'passive_checks_enabled': 1,
                'event_handler_enabled': 1,
                'flap_detection_enabled': 1,
                'process_perf_data': 1,
                'retain_status_information': 1,
                'retain_nonstatus_information': 1,
                'notification_interval': 0,
                'notification_options': 'a',
                'notifications_enabled': 1,
                'register': 0,
                'name': 'default-host',
            }],
            'service': [{
                'is_volatile': 0,
                'max_check_attempts': 3,
                'check_interval': 5,
                'check_command': 'dummy_command',
                'retry_interval': 1,
                'active_checks_enabled': 0,
                'passive_checks_enabled': 1,
                'check_period': '24x7',
                'parallelize_check': 0,
                'obsess': 0,
                'check_freshness': 0,
                'event_handler_enabled': 1,
                'flap_detection_enabled': 1,
                'process_perf_data': 1,
                'retain_status_information': 1,
                'retain_nonstatus_information': 1,
                'notification_interval': 0,
                'notification_period': '24x7',
                'notification_options': 'a',
                'notifications_enabled': 1,
                'register': 0,
                'name': 'default-service'
            }]
        }

    def add_obj(self, object_type, parameters):
        if object_type not in self.current_config:
            self.current_config[object_type] = []
        object = dict()
        for row in parameters:
            for heading in row.headings:
                object[heading] = row[heading]
        self.current_config[object_type].append(object)

    def to_string(self):
        res = ""
        for type, objects in self.current_config.items():
            for object in objects:
                res += ('define %s {\n' % (type))
                for key, value in object.items():
                    res += ('\t%s\t%s\n' % (key, value))
                res += ('}\n\n')
        return res

    def to_file(self, filename):
        f = open(filename, 'w')
        f.write(self.to_string())
        f.close()
