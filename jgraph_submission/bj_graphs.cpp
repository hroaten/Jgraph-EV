#include <iostream>
#include <vector>
#include <fstream>

using namespace std;
int main(int argc, char** argv){

    int interactive = 0;
    //get some arguments, table min, table max, bankroll, interactive- study or practice, bet spread? 
    //generate some nuber of random deck configurations, plot EV per hand per count at intervals of 0.5
    //Non interactive- Just display EV graphs
    //Interactive:
        //play through random decks and collect AV data
        //study- tell player best move/ offer hint
        //practice- don't do that
    //Include ROR?

    //Assuming CSV file exists
    ifstream ev_data("evs.csv", 'r');
    if(interactive){
        ifstream av_data("avs.csv", 'r');
    }

    ofstream jgraph("bj_<ruleset>_<study/practice>_results.jgraph");
    //Table for each player hand vs each dealer up card
    //each block in the table is a graph of the EV for that hand from count -5 to 5
    //Each graph is colored based on the highest EV move for that shand at that count, 
    //color changes for basic strategy variatons
    //Interactive mode- separate graph with EV from random decks, versus AV from player input
    //EV follows same coloring as non-interactive mode, AV is green for highest EV move, red for lowest
    //x-axis is turns, ticks mark a player move, label new rounds with (initial total, upcard)
    //y-axis is Value
    //plot points where count shifts to next rounding of .5





}