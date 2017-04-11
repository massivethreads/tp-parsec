// annealer_thread.cpp
//
// Created by Daniel Schwartz-Narbonne on 14/04/07.
//
// Copyright 2007-2008 Princeton University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

#if defined ENABLE_THREADS || defined ENABLE_TASK
#include <pthread.h>
#endif

#undef sync

#include <cassert>
#include "annealer_thread.h"
#include "location_t.h"
#include "annealer_types.h"
#include "netlist_elem.h"
#include <math.h>
#include <iostream>
#include <fstream>
#include "rng.h"
#ifdef ENABLE_TASK
#include <tp_parsec.h>
#include <list> 
#include <algorithm>
#endif

using std::cout;
using std::endl;


//*****************************************************************************************
// Task parallel version of the annealing algorithm
//*****************************************************************************************
#ifdef ENABLE_TASK
void annealer_thread::Run()
{
	cilk_begin;
	int accepted_good_moves=0;
	int accepted_bad_moves=-1;
	double T = _start_temp;
	int temp_steps_completed=0; 
	while(keep_going(temp_steps_completed, accepted_good_moves, accepted_bad_moves)){
		T = T / 1.5;
		accepted_good_moves = 0;
		accepted_bad_moves = 0;
		int movesLeftInThisTemp = _moves_per_thread_temp;
		mk_task_group;
		std::list<int> local_accepted_good_moves; // use list because insertion of new elements does not invalidate existing iterators
		std::list<int> local_accepted_bad_moves;
		while(movesLeftInThisTemp > _cutoff)
		{
			local_accepted_good_moves.push_back(0);
			local_accepted_bad_moves.push_back(0);
			create_taskA(spawn doMoves(_cutoff,T,local_accepted_good_moves.front(),local_accepted_bad_moves.front()));
			movesLeftInThisTemp -= _cutoff;
		}
		local_accepted_good_moves.push_back(0);
		local_accepted_bad_moves.push_back(0);
		call_task(spawn doMoves(movesLeftInThisTemp,T,local_accepted_good_moves.front(),local_accepted_bad_moves.front()));
		wait_tasks;
		accepted_good_moves = std::accumulate(local_accepted_good_moves.begin(),local_accepted_good_moves.end(),0);
		accepted_bad_moves = std::accumulate(local_accepted_bad_moves.begin(),local_accepted_bad_moves.end(),-1);
		temp_steps_completed++;
	}
	cilk_void_return;
}
#endif

//*****************************************************************************************
// Do a specified number of element moves
//*****************************************************************************************
#ifdef ENABLE_TASK
void annealer_thread::doMoves(const int numMoves, const double T, int& accepted_good_moves, int& accepted_bad_moves)
{
	Rng rng;
	long a_id;
	long b_id;
	netlist_elem* a = _netlist->get_random_element(&a_id, NO_MATCHING_ELEMENT, &rng);
	netlist_elem* b = _netlist->get_random_element(&b_id, NO_MATCHING_ELEMENT, &rng);

	for (int i = 0; i < numMoves; i++){
	//get a new element. Only get one new element, so that reuse should help the cache
		a = b;
		a_id = b_id;
		b = _netlist->get_random_element(&b_id, a_id, &rng);
		
		routing_cost_t delta_cost = calculate_delta_routing_cost(a,b);
		move_decision_t is_good_move = accept_move(delta_cost, T, &rng);

		//make the move, and update stats:
		if (is_good_move == move_decision_accepted_bad){
			accepted_bad_moves++;
			_netlist->swap_locations(a,b);
		} else if (is_good_move == move_decision_accepted_good){
			accepted_good_moves++;
			_netlist->swap_locations(a,b);
		} else if (is_good_move == move_decision_rejected){
			//no need to do anything for a rejected move
		}
	}
	
}
#endif

#ifndef ENABLE_TASK 
//*****************************************************************************************
// Do the actual anealing process. Original pthreads version 
//*****************************************************************************************
void annealer_thread::Run()
{
	int accepted_good_moves=0;
	int accepted_bad_moves=-1;
	double T = _start_temp;
	Rng rng; //store of randomness
	
	long a_id;
	long b_id;
	netlist_elem* a = _netlist->get_random_element(&a_id, NO_MATCHING_ELEMENT, &rng);
	netlist_elem* b = _netlist->get_random_element(&b_id, NO_MATCHING_ELEMENT, &rng);

	int temp_steps_completed=0; 
	while(keep_going(temp_steps_completed, accepted_good_moves, accepted_bad_moves)){
		T = T / 1.5;
		accepted_good_moves = 0;
		accepted_bad_moves = 0;
		
		for (int i = 0; i < _moves_per_thread_temp; i++){
			//get a new element. Only get one new element, so that reuse should help the cache
			a = b;
			a_id = b_id;
			b = _netlist->get_random_element(&b_id, a_id, &rng);
			
			routing_cost_t delta_cost = calculate_delta_routing_cost(a,b);
			move_decision_t is_good_move = accept_move(delta_cost, T, &rng);

			//make the move, and update stats:
			if (is_good_move == move_decision_accepted_bad){
				accepted_bad_moves++;
				_netlist->swap_locations(a,b);
			} else if (is_good_move == move_decision_accepted_good){
				accepted_good_moves++;
				_netlist->swap_locations(a,b);
			} else if (is_good_move == move_decision_rejected){
				//no need to do anything for a rejected move
			}
		}
		temp_steps_completed++;
#ifdef ENABLE_THREADS
		pthread_barrier_wait(&_barrier);
#endif
	}
}
#endif

//*****************************************************************************************
//
//*****************************************************************************************
annealer_thread::move_decision_t annealer_thread::accept_move(routing_cost_t delta_cost, double T, Rng* rng)
{
	//always accept moves that lower the cost function
	if (delta_cost < 0){
		return move_decision_accepted_good;
	} else {
		double random_value = rng->drand();
		double boltzman = exp(- delta_cost/T);
		if (boltzman > random_value){
			return move_decision_accepted_bad;
		} else {
			return move_decision_rejected;
		}
	}
}


//*****************************************************************************************
//  If get turns out to be expensive, I can reduce the # by passing it into the swap cost fcn
//*****************************************************************************************
routing_cost_t annealer_thread::calculate_delta_routing_cost(netlist_elem* a, netlist_elem* b)
{
	location_t* a_loc = a->present_loc.Get();
	location_t* b_loc = b->present_loc.Get();
	
	routing_cost_t delta_cost = a->swap_cost(a_loc, b_loc);
	delta_cost += b->swap_cost(b_loc, a_loc);

	return delta_cost;
}

//*****************************************************************************************
//  Check whether design has converged or maximum number of steps has reached
//*****************************************************************************************
bool annealer_thread::keep_going(int temp_steps_completed, int accepted_good_moves, int accepted_bad_moves)
{
	bool rv;

	if(_number_temp_steps == -1) {
		//run until design converges
		rv = _keep_going_global_flag && (accepted_good_moves > accepted_bad_moves);
		if(!rv) _keep_going_global_flag = false; // signal we have converged
	} else {
		//run a fixed amount of steps
		rv = temp_steps_completed < _number_temp_steps;
	}

	return rv;
}

