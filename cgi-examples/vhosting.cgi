#!/bin/sh
#
# Some gopher daemons try to emulate some vhosting, by checking, if some
# request path only exists in some directory of a specific host.
#

basevhosts="/gopher/vhosts"

for i in $(find "${basevhosts}" -maxdepth 1 -type d);
do
	# Check if request exists in some vhost dir.
	if [ -e "${i}/${2}" ];
	then
		vhost="$(basename "${i}")"
		printf "Our vhost is %s!\n" "${vhost}"
		printf "Thank you for flying gopher airlines!\n"

		# Serve content accordingly.

		exit 0
	fi
done

