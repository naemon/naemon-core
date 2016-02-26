if [ -f .version_number ]; then
	cat .version_number | tr -d '\n'
	exit
fi
git describe --tags | sed 's/^v\([0-9.]*\).*$/\1/g' | tr -d '\n'
