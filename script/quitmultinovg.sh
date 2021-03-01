#!/bin/bash

if [[ $# -ne 1 ]];
	then printf "USAGE: %s #numberoftests\n" ${0##*/}
	exit 1	
fi

programname=./bin/supermarket

for (( i=0;i<$1;i++))
do
	printf "%d: " $i;
	$programname &
	sleep 5;
	kill -s 3 $!;
	wait $!;
	printf "test completed\n" 
done

exit 0