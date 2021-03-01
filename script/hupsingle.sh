#!/bin/bash

if [[ $# -ne 2 ]];
	then printf "USAGE: %s #numberoftests coreid\n" ${0##*/}
	exit 1	
fi

programname=./bin/supermarket

logname=./logs/${0##*/}
logname=${logname%.sh}.log

for (( i=0;i<$1;i++))
do
	printf "%d: " $i;
	taskset -c $2 \
	valgrind --quiet --leak-check=full --show-leak-kinds=all \
	--log-fd=7 7>>$logname $programname &
	sleep 5;
	kill -s 1 $!;
	wait $!;
	printf "test completed\n" 
done

exit 0