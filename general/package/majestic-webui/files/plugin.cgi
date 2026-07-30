#!/bin/sh
#
# One command to majestic's plugin socket, and its reply.
#
# The plugin interface is a plain TCP command socket on port 4000, which a
# browser cannot open, so the settings page needs something on the HTTP side to
# reach it through. Nothing here interprets the command - the plugin owns what
# the commands mean, and this only carries them.
#
#   /cgi-bin/j/plugin.cgi?cmd=brightness&val=128
#
# Both fields are validated strictly rather than escaped: letters only for the
# command, digits only for the value. Anything else is refused. That is worth
# more than it looks, because the two go on to a command socket, and a rule that
# only accepts what is known good cannot be talked round by an encoding trick.

echo "HTTP/1.1 200 OK
Content-type: text/plain; charset=UTF-8
Cache-Control: no-store
Pragma: no-cache

"

cmd=""
val=""
for param in $(echo "$QUERY_STRING" | tr '&' ' '); do
	case "$param" in
		cmd=*) cmd="${param#*=}" ;;
		val=*) val="${param#*=}" ;;
	esac
done

case "$cmd" in
	''|*[!a-z]*)
		echo "bad command"
		exit 1
		;;
esac

case "$val" in
	*[!0-9]*)
		echo "bad value"
		exit 1
		;;
esac

# -w bounds the wait: the plugin answers and majestic closes, but a streamer
# that is restarting can accept and then say nothing, and this must not sit
# there holding a CGI process open.
echo "$cmd $val" | nc -w 2 127.0.0.1 4000
