#!/bin/sh
#
# Be sure to make this some index.cgi inside your /scm base dir.
# Then this will mirror /scm on the $proxyhost:$proxyport here.
#

arguments="${2}"
proxyhost="192.168.4.56"
proxyport="70"
proxybase="/scm"

# git://bitreich.org/hurl
hurl gopher://$proxyhost:$proxyport/9${proxybase}${arguments}

