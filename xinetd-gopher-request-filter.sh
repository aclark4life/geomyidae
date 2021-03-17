#!/bin/sh
#
# Copy me if you can.
#
# Change to fit to your original running geomyidae instance.
dstserver="localhost"
dstport="7070"

read -r request
request="$(printf "%s\n" "${request}" | tr -d '\r')"
case "${request}" in
*bill-gates*|*cia*)
	printf "3The request cannot be handled\terror\t70\r\n"
	;;
*)
	printf "%s\r\n" "${request}" | nc "${dstserver}" "${dstport}"
	;;
esac

