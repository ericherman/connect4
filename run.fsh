#!/usr/local/bin/fish

set NUM_GENERATIONS 7

echo "Generating up to $NUM_GENERATIONS generations."

rm -rf results
mkdir results

make clean
make MODE=NDEBUG all

# create gen 0 (database with single, empty board)
./bfcf -d results/num_moves_0 -c e

for g in (seq $NUM_GENERATIONS)
	echo "Generation $g"
	set prev (math $g - 1)

	./bfcf -d results/num_moves_$prev -n results/num_moves_$g
	and mv gencounter.gc results/gencounter_$g.gc
	
	# save diskspace by not keeping old stuff
#	rm results/generation_$prev.c4
end

echo -e "\nResults:"

./bfcf -d results/ -g
./bfcf -d results/ -g > latest_results.txt
