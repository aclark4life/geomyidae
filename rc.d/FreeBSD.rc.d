#!/bin/sh

# PROVIDE: geomyidae
# REQUIRE: DAEMON
# BEFORE:  LOGIN
# KEYWORD: shutdown


. /etc/rc.subr

name=geomyidae
rcvar=$name

command="/usr/local/bin/$name"

geomyidae_enable=${geomyidae_enable:-"NO"}

#####################################################
# Geomyidae Options Section - "?" => geomyidae(8)   #
#  Uncomment & define options (defaults are shown)  #
#####################################################
#
#LOGFILE="-l /var/log/gopherlog"
#LOGLEVEL="-v 7"
#HTDOCS="-b /var/gopher"
#PORT="-p 70"
#SPORT="-o 70"
#USR="-u $USER"
#GRP="-g $GROUP"
#HOST="-h localhost"
#IP="-i 127.0.0.1"

######################################################
# Next, add all DEFINED options to command_args=     #
######################################################
#
#command_args="$LOGFILE $LOGLEVEL $HTDOCS $PORT $SPORT $USR $GRP $HOST $IP"
#command_args=""


######################################################
#  Uncomment this section if a PID file is desired   #
######################################################

#pidfile="/var/run/${name}.pid"
#start_cmd="geomyidae_start"
#
#geomyidae_start()
#{
#        echo "Starting $name"
#        $command $command_args
#        pgrep -n $name > $pidfile
#}

######################################################
#  Lastly, add the following to /etc/rc.conf:        #
#  "geomyidae=YES"  (without the quotes)             #
######################################################

load_rc_config $name
run_rc_command "$1"
