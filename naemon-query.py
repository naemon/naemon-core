#!/usr/bin/env python

import sys, os, socket

class naemon_qh:
	__slots__ = ['query_handler', 'socket', 'read_size']
	read_size = 4096
	socket = False
	query_handler = False

	def __init__(self, query_handler):
		self.query_handler = query_handler

	def query(self, query):
		"""Ask a raw query to naemon' query handler socket, return the raw
		response as a lazily generated sequence."""
		if not query:
			print "Missing query argument"
			return
		try:
			self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
			self.socket.connect(self.query_handler)

			self.socket.send(query + '\0')
			while True:
				try:
					out = self.socket.recv(self.read_size)
					if not out:
						break
					yield out
				except KeyboardInterrupt:
					print "Good bye."
					break
			self.socket.close()
		except socket.error, e:
			print "Couldn't connect to naemon socket %s: %s" % (self.query_handler, str(e))

	def format(self, data, rowsep='\n', pairsep=';', kvsep='='):
		"""Lazily format a response into a sequence of dicts"""
		def todict(row):
			return dict(x.split(kvsep) for x in row.split(pairsep))
		rest = ''
		for block in data:
			block = rest + block
			rows = block.split(rowsep)
			for row in rows[:-1]:
				yield todict(row)
			rest = rows[-1]
		if rest:
			yield todict(rest)

	def get(self, query, rowsep='\n', pairsep=';', kvsep='='):
		"""Ask a query to naemon' query handler socket, and return an object
		representing a "standard" naemon qh response"""
		resp = self.query(query)
		return self.format(resp, rowsep, pairsep, kvsep)


if __name__ == '__main__':
	socket_path = '/var/lib/naemon/naemon.qh'
	query_args = []

	for arg in sys.argv[1:]:
		if arg.startswith('--socket='):
			socket_path = arg.split('=', 1)[1]
			continue
		query_args.append(arg)

	query = ' '.join(query_args)
	print("Socket: %s\nQuery : %s" % (socket_path, query))
	qh = naemon_qh(socket_path)
	for block in qh.query(query):
		sys.stdout.write(block)
		sys.stdout.flush()
