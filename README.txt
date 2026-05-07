Holden Roaten COSC494 Lab 1: Jgraph

Blackjack: 
This program is designed to calculate the expected value of any hand during a game of blackjack.
Each EV is calculated from the current state of the shoe for the given hand, but since every shuffling 
of a blackjack shoe is independent, an immediate EV from one hand is not definitive of the optimal playing strategy for an identical hand
drawn from a different shoe. 
To Account for this variability the program implements two important features:

Generate "Base" EV's:the program samples 250 Blackjack shoes that have a "True Count" approximal every True count at 0.5 step increments from the 
minimum TC provided to the maximum TC provided. This process is slow, and uses OMP to parallelize some of the work, to simplify the grading process,
the base_ev's for my "basic" blackjack ruleset is included.

Accumulate EV's over time: The driver program which actually allows user to play the game, also calculates new ev's in real time as the user
encounters new, unique deck compositions. The user is not provided with the exact EV of their hand because in a real casino they would not have access
to the knowledge required to recieve these number, but they are provided an estimate based on the TC of the deck they are playing after it's EV has 
been added to the cumulative ev file.

The graphical representations of this data are laid out in the same format many gamblers will be familair with: a blackjack basic strategy chart
For each initially dealt hand of hard totals 5-20, soft totals 13-20, and all pairs versus each possible Dealer's up-card, 
the EV of each legal move is graphed from TC = -5 to TC = +5

If the graphs are created with the "lines" option, each individual move is plotted as its own curve
If the graphs are created with the "optimal" option, each plot is a single curve colored based on the optimal move, where color changes represent a 
shift in optimal strategy when the deck reaches a specific TC value

Graph's can be created from the base file or the cumulative file which grows in accuracy as the program is utilized more. With significant use, 
user's can generate both to see the difference in approximation accuracy over time.

Future work:
Allow users to input individual game rules (decks, cut, DAS, etc) to generate EV's for whatever particular blackjack rule variation's they feel like playing
Allow users to input bankroll, bet-spread, or both and calculate Risk of Ruin or ideal bet-spread
Genrate graphics for users to compare the EV of each hand they've played in a session to the optimal ev and the Actual result of the hand(s)


Compile and run:
As mentioned above, generating base_ev's is time consuming so the EV/basic_base.bj and EV/basic.bj files are included, but they can be removed and generated
in ~5 minutes (Assuming OMP is available)

Gameplay: 
"make" compiles Blackjack and Plot_EV binaries

Graphs:
./Plot_EV <ruleset> <hard|soft|pairs> <lines|optimal> <base|cumulative> > /out/your_output_file.jgr
sh render.sh converts all .jgr files in /out/ to pdf
**ruleset must be "basic" as ruleset variations have not yet been implemented, the cumulative graphs will be indentical to the base graphs unless
you play enough blackjack to update the values





