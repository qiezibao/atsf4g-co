<%!
    import common.project_utils as project
%><%include file="common.template.sh" />

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -eq $? ]; then
	NoticeMsg "$SERVER_FULL_NAME already stopped";
	exit 0;
fi

./$SERVERD_NAME -id $SERVER_BUS_ID -c ../etc/$SERVER_FULL_NAME.conf -p $SERVER_PID_FILE_NAME stop

export LD_PRELOAD=;

if [ $? -ne 0 ]; then
	ErrorMsg "send stop command to $SERVER_FULL_NAME failed.";
	exit $?;
fi

WaitProcessStoped "$SERVER_PID_FILE_NAME";
NoticeMsg "stop $SERVER_FULL_NAME done." ;