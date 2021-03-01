#!/bin/bash

file=./logs/supermarket.log

#use . (dot) in printf float even in systems
#   that use , (comma) by default
LC_NUMERIC="en_US.UTF-8"

tab=$(printf '\t')

declare -a cashier_total_time_to_serve
declare -a cashier_total_time_open
declare -a cashier_elaborated_prodcuts
declare -a cashier_served_customers
declare -a cashier_closures_count

printf "Customers:\n"
printf "┌────────┬────────┬────────┬────────┬────────┐\n"
printf "│   ID   │ #PRODS │SPRMTIME│SRVSTIME│ #QUEUE │\n"
printf "├────────┼────────┼────────┼────────┼────────┤\n"

while IFS=$'\n' read -r line; do

	IFS=${tab}

	case ${line:0:2} in

		"C${tab}")
			read -r code customer_id time_supermarket time_queue\
					changed_queues products_count <<< "$line"
			printf "│ %6d │ %6d │ %6.3f │ %6.3f │ %6d │\n" \
				"$customer_id" "$products_count" "$time_supermarket" \
				"$time_queue" "$changed_queues"
			;;

		"K${tab}")
			read -r code cashier_id served_customers\
					elaborated_products closures_count <<< "$line"
			cashier_elaborated_prodcuts[$cashier_id]=$elaborated_products
			cashier_served_customers[$cashier_id]=$served_customers
			cashier_closures_count[$cashier_id]=$closures_count
			;;

		"KC")
			read -r code cashier_id customer_id \
					time_to_serve <<< "$line"

			if [ -z "${cashier_total_time_to_serve[$cashier_id]}" ]
			then
				cashier_total_time_to_serve[$cashier_id]=0
			fi

			temp=$(echo "${cashier_total_time_to_serve[${cashier_id}]} + ${time_to_serve}" | bc -l)
			cashier_total_time_to_serve[$cashier_id]=$temp
			;;

		"KS")
			read -r code cashier_id time_workshift <<< "$line"

			if [ -z "${cashier_total_time_open[$cashier_id]}" ]
			then
				cashier_total_time_open[$cashier_id]=0
			fi

			temp=$(echo "${cashier_total_time_open[${cashier_id}]} + ${time_workshift}" | bc -l)
			cashier_total_time_open[$cashier_id]=$temp
			;;
		*)
			;;
			
	esac

done < "$file"

printf "└────────┴────────┴────────┴────────┴────────┘\n"


printf "\n"
printf "Cashiers: \n"
printf "┌────────┬────────┬────────┬────────┬────────┬────────┐\n"
printf "│   ID   │ #PRODS │#CUSTMRS│OPENTIME│AVGSERVC│ #CLOSE │\n"
printf "├────────┼────────┼────────┼────────┼────────┼────────┤\n"

cashiers_count=${#cashier_closures_count[@]}

for (( i=0; i<cashiers_count; i++ ))
do

	cashier_avg_time_to_serve=0

	if [ "${cashier_served_customers[$i]}" -ne 0 ]
	then
		cashier_avg_time_to_serve=$(echo "${cashier_total_time_to_serve[$i]} / ${cashier_served_customers[$i]}" | bc -l)
	fi

	printf "│ %6d │ %6d │ %6d │ %6.3f │ %6.3f │ %6d │\n" \
		"$i" "${cashier_elaborated_prodcuts[$i]}" \
		"${cashier_served_customers[$i]}" "${cashier_total_time_open[$i]}" \
		"$cashier_avg_time_to_serve" "${cashier_closures_count[$i]}"

done

printf "└────────┴────────┴────────┴────────┴────────┴────────┘\n"
