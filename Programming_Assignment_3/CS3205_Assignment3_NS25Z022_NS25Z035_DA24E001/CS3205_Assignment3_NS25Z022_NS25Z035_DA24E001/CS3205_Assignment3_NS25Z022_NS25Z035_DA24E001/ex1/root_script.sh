#!/bin/bash

# File containing country names and URLs
input_file="countries_websites.txt"

# Create the outputs folder if it doesn't exist
output_dir="new_outputs"
mkdir -p "$output_dir"

# Read the file line by line
while IFS= read -r country && IFS= read -r url; do
    # Run the command on the URL and save the output to a file named after the country
    output_file="${output_dir}/${country}.txt"
    
    # Replace this line with your desired command
    for i in `seq 10`; do traceroute -n "$url"; done > "$output_file" &
    
    # Example: Additional processing on the downloaded content
    # grep "keyword" "$output_file" > "${output_dir}/${country}_filtered.txt" &
done < "$input_file"

# Wait for all background processes to finish
wait