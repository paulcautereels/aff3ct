#ifdef SYSTEMC

#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cassert>
#include <algorithm>

#include "Tools/Display/bash_tools.h"

#include "Tools/Factory/Factory_source.hpp"
#include "Tools/Factory/Factory_CRC.hpp"
#include "Tools/Factory/Factory_encoder_coset.hpp"
#include "Tools/Factory/Factory_encoder_AZCW.hpp"
#include "Tools/Factory/Factory_modulator.hpp"
#include "Tools/Factory/Factory_channel.hpp"
#include "Tools/Factory/Factory_quantizer.hpp"
#include "Tools/Factory/Factory_interleaver.hpp"
#include "Tools/Factory/Coset/Factory_coset_real.hpp"
#include "Tools/Factory/Coset/Factory_coset_bit.hpp"
#include "Tools/Factory/Factory_monitor.hpp"
#include "Tools/Factory/Factory_terminal.hpp"

#include "Tools/Algo/Predicate_ite.hpp"

#include "SC_Simulation_BFERI.hpp"

template <typename B, typename R, typename Q>
Simulation_BFERI<B,R,Q>
::Simulation_BFERI(const parameters& params)
: Simulation(),
  
  params(params),

  barrier(params.simulation.n_threads),
  n_frames(1),
 
  snr      (0.f),
  code_rate(0.f),
  sigma    (0.f),

  X_N1(params.simulation.n_threads), // hack for the compilation but never used

  source       (1, nullptr),
  crc          (1, nullptr),
  encoder      (1, nullptr),
  interleaver_e(   nullptr),
  modulator    (1, nullptr),
  channel      (1, nullptr),
  quantizer    (1, nullptr),
  interleaver  (1, nullptr),
  coset_real   (1, nullptr),
  coset_real_i (   nullptr),
  siso         (1, nullptr),
  decoder      (1, nullptr),
  coset_bit    (1, nullptr),
  monitor      (1, nullptr),
  terminal     (   nullptr),

  duplicator{nullptr, nullptr, nullptr, nullptr, nullptr},
  router     (nullptr),
  predicate  (nullptr),

  dbg_B{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
  dbg_R{nullptr, nullptr, nullptr},
  dbg_Q{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},

  d_decod_total_fake(std::chrono::nanoseconds(0))
{
	if (params.simulation.n_threads > 1)
	{
		std::cerr << bold_red("(EE) SystemC simulation does not support multi-threading... Exiting.") << std::endl;
		std::exit(-1);
	}

	if (params.simulation.benchs)
	{
		std::cerr << bold_red("(EE) SystemC simulation does not support the bench mode... Exiting") << std::endl;
		std::exit(-1);
	}

	if (params.simulation.time_report)
		std::cerr << bold_yellow("(WW) The time report is not available in the SystemC simulation.") << std::endl;
}

template <typename B, typename R, typename Q>
Simulation_BFERI<B,R,Q>
::~Simulation_BFERI()
{
	release_objects();
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::launch()
{
	launch_precompute();
	
	// for each SNR to be simulated
	for (snr = params.simulation.snr_min; snr <= params.simulation.snr_max; snr += params.simulation.snr_step)
	{
		t_snr = std::chrono::steady_clock::now();

		code_rate = (float)(params.code.K / (float)(params.code.N + params.code.tail_length));
		sigma     = std::sqrt((float)params.modulator.upsample_factor) /
		            std::sqrt(2.f * code_rate * (float)params.modulator.bits_per_symbol * std::pow(10.f, (snr / 10.f)));

		snr_precompute();

		// launch the simulation
		this->launch_simulation();

		if (!params.terminal.disabled && !params.simulation.benchs)
			terminal->final_report(std::cout);

		// release communication objects
		release_objects();

		// exit simulation (double [ctrl+c])
		if (Monitor<B>::is_over())
			break;
	}
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::launch_simulation()
{
	this->build_communication_chain();

	if ((!this->params.terminal.disabled && this->snr == this->params.simulation.snr_min &&
	    !(this->params.simulation.debug && this->params.simulation.n_threads == 1) && !this->params.simulation.benchs))
		this->terminal->legend(std::cout);

	Predicate_ite p(this->params.demodulator.n_ite);

	this->duplicator[0] = new SC_Duplicator(   "Duplicator0");
	this->duplicator[1] = new SC_Duplicator(   "Duplicator1");
	if (this->params.code.coset)
	{
		this->duplicator[2] = new SC_Duplicator("Duplicator2");
		this->duplicator[3] = new SC_Duplicator("Duplicator3");
		this->duplicator[4] = new SC_Duplicator("Duplicator4");
	}
	this->router        = new SC_Router    (p, "Router"     );
	this->predicate     = new SC_Predicate (p, "Predicate"  );

	if (this->params.simulation.n_threads == 1 && this->params.simulation.debug)
	{
		const auto dl = this->params.simulation.debug_limit;

		this->dbg_B[0] = new SC_Debug<B>("Generate random bits U_K...               \nU_K: \n", dl, "Debug_B0");
		this->dbg_B[1] = new SC_Debug<B>("Add the CRC to U_K...                     \nU_K: \n", dl, "Debug_B1");
		this->dbg_B[2] = new SC_Debug<B>("Encode U_K in X_N1...                     \nX_N1:\n", dl, "Debug_B2");
		this->dbg_B[3] = new SC_Debug<B>("Interleave X_N1 in X_N2...                \nX_N2:\n", dl, "Debug_B3");
		this->dbg_R[0] = new SC_Debug<R>("Modulate X_N2 in X_N3...                  \nX_N3:\n", dl, "Debug_R0");
		this->dbg_R[1] = new SC_Debug<R>("Add noise from X_N3 to Y_N1...            \nY_N1:\n", dl, "Debug_R1");
		this->dbg_R[2] = new SC_Debug<R>("Filter from Y_N1 to Y_N2...               \nY_N2:\n", dl, "Debug_R2");
		this->dbg_Q[0] = new SC_Debug<Q>("Make the quantization from Y_N2 to Y_N3...\nY_N3:\n", dl, "Debug_Q0");
		this->dbg_Q[1] = new SC_Debug<Q>("Demodulate from Y_N3 and Y_N7 to Y_N4...  \nY_N4:\n", dl, "Debug_Q1");
		this->dbg_Q[2] = new SC_Debug<Q>("Deinterleave from Y_N4 to Y_N5...         \nY_N5:\n", dl, "Debug_Q2");
		this->dbg_Q[3] = new SC_Debug<Q>("Soft decode from Y_N5 to Y_N6...          \nY_N6:\n", dl, "Debug_Q3");
		this->dbg_Q[4] = new SC_Debug<Q>("Interleave from Y_N6 to Y_N7...           \nY_N7:\n", dl, "Debug_Q4");
		this->dbg_B[4] = new SC_Debug<B>("Hard decode Y_N5 and generate V_K...      \nV_K: \n", dl, "Debug_B4");

		if (this->params.code.coset)
		{
			this->dbg_Q[5] = new SC_Debug<Q>("Apply the coset approach on Y_N5...       \nY_N5:\n", dl, "Debug_Q5");
			this->dbg_Q[6] = new SC_Debug<Q>("Reverse the coset on Y_N6...              \nY_N6:\n", dl, "Debug_Q6");
			this->dbg_B[5] = new SC_Debug<B>("Apply the coset approach on V_K...        \nV_K: \n", dl, "Debug_B5");
		}

		this->bind_sockets_debug();
		sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DO_NOTHING);
		sc_core::sc_start(); // start simulation
		this->terminal->legend(std::cout);

		for (auto i = 0; i < 6; i++)
			if (this->dbg_B[i] != nullptr)
			{
				delete this->dbg_B[i];
				this->dbg_B[i] = nullptr;
			}

		for (auto i = 0; i < 3; i++)
			if (this->dbg_R[i] != nullptr)
			{
				delete this->dbg_R[i];
				this->dbg_R[i] = nullptr;
			}

		for (auto i = 0; i < 7; i++)
			if (this->dbg_Q[i] != nullptr)
			{
				delete this->dbg_Q[i];
				this->dbg_Q[i] = nullptr;
			}
	}
	else
	{
		// launch a thread dedicated to the terminal display
		std::thread thread(Simulation_BFERI<B,R,Q>::terminal_temp_report, this);

		this->bind_sockets();
		sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DO_NOTHING);
		sc_core::sc_start(); // start simulation

		// wait the terminal thread to finish
		thread.join();
	}

	for (auto i = 0; i < 5; i++)
		if (this->duplicator[i] != nullptr)
		{
			delete this->duplicator[i];
			this->duplicator[i] = nullptr;
		}

	delete this->router;      this->router      = nullptr;
	delete this->predicate;   this->predicate   = nullptr;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// /!\ VERY DIRTY WAY TO CREATE A NEW SIMULATION CONTEXT IN SYSTEMC, BE CAREFUL THIS IS NOT IN THE STANDARD! /!\ //
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	sc_core::sc_curr_simcontext = new sc_core::sc_simcontext();
	sc_core::sc_default_global_context = sc_core::sc_curr_simcontext;
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::build_communication_chain()
{
	// build the objects
	this->source     [0] = this->build_source     (     ); check_errors(this->source     [0], "Source<B>"       );
	this->crc        [0] = this->build_crc        (     ); check_errors(this->crc        [0], "CRC<B>"          );
	this->encoder    [0] = this->build_encoder    (     ); check_errors(this->encoder    [0], "Encoder<B>"      );
	this->interleaver[0] = this->build_interleaver(     ); check_errors(this->interleaver[0], "Interleaver<int>");
	this->interleaver_e  = this->build_interleaver(     ); check_errors(this->interleaver_e , "Interleaver<int>");
	this->modulator  [0] = this->build_modulator  (     ); check_errors(this->modulator  [0], "Modulator<B,R,Q>");

	assert(*this->interleaver[0] == *this->interleaver_e);
	this->interleaver_e->rename("Interleaver_e");

	const auto N     = this->params.code.N;
	const auto tail  = this->params.code.tail_length;
	const auto N_mod = this->modulator[0]->get_buffer_size_after_modulation(N + tail);
	const auto N_fil = this->modulator[0]->get_buffer_size_after_filtering (N + tail);

	this->channel    [0] = this->build_channel    (N_mod); check_errors(this->channel    [0], "Channel<R>"      );
	this->quantizer  [0] = this->build_quantizer  (N_fil); check_errors(this->quantizer  [0], "Quantizer<R,Q>"  );
	this->coset_real [0] = this->build_coset_real (     ); check_errors(this->coset_real [0], "Coset<B,Q>"      );
	this->coset_real_i   = this->build_coset_real (     ); check_errors(this->coset_real_i  , "Coset<B,Q>"      );
	this->siso       [0] = this->build_siso       (     ); check_errors(this->siso       [0], "SISO<Q>"         );
	this->decoder    [0] = this->build_decoder    (     ); check_errors(this->decoder    [0], "Decoder<B,Q>"    );
	this->coset_bit  [0] = this->build_coset_bit  (     ); check_errors(this->coset_bit  [0], "Coset<B,B>"      );
	this->monitor    [0] = this->build_monitor    (     ); check_errors(this->monitor    [0], "Monitor<B>"      );

	this->coset_real_i->rename("Coset_real_i");

	// create the sc_module inside the objects of the communication chain
	this->source     [0]->create_sc_module              ();
	this->crc        [0]->create_sc_module              ();
	this->encoder    [0]->create_sc_module              ();
	this->interleaver_e ->create_sc_module_interleaver  ();
	this->modulator  [0]->create_sc_module_modulator    ();
	this->channel    [0]->create_sc_module              ();
	this->modulator  [0]->create_sc_module_filterer     ();
	this->quantizer  [0]->create_sc_module              ();
	this->modulator  [0]->create_sc_module_tdemodulator ();
	this->interleaver[0]->create_sc_module_deinterleaver();
	this->siso       [0]->create_sc_module_siso         ();
	this->interleaver[0]->create_sc_module_interleaver  ();
	this->decoder    [0]->create_sc_module              ();
	this->monitor    [0]->create_sc_module              ();
	if (this->params.code.coset)
	{
		this->coset_real[0]->create_sc_module();
		this->coset_real_i ->create_sc_module();
		this->coset_bit [0]->create_sc_module();
	}

	// get the real number of frames per threads (from the decoder)
	this->n_frames = this->decoder[0]->get_n_frames();
	assert(this->siso[0]->get_n_frames() == this->decoder[0]->get_n_frames());

	// set the real number of frames per thread
	this->source     [0]->set_n_frames(this->n_frames);
	this->crc        [0]->set_n_frames(this->n_frames);
	this->encoder    [0]->set_n_frames(this->n_frames);
	this->interleaver[0]->set_n_frames(this->n_frames);
	this->interleaver_e ->set_n_frames(this->n_frames);
	this->modulator  [0]->set_n_frames(this->n_frames);
	this->channel    [0]->set_n_frames(this->n_frames);
	this->quantizer  [0]->set_n_frames(this->n_frames);
	this->coset_real [0]->set_n_frames(this->n_frames);
	this->coset_real_i  ->set_n_frames(this->n_frames);
	this->coset_bit  [0]->set_n_frames(this->n_frames);
	this->monitor    [0]->set_n_frames(this->n_frames);

	// build the terminal to display the BER/FER
	this->terminal = this->build_terminal();
	check_errors(this->terminal, "Terminal");
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::bind_sockets()
{
	if (this->params.code.coset)
	{
		this->source     [0]->module        ->s_out (this->crc        [0]->module        ->s_in );
		this->crc        [0]->module        ->s_out (this->duplicator [0]                ->s_in );
		this->duplicator [0]                ->s_out1(this->duplicator [2]                ->s_in );
		this->duplicator [2]                ->s_out1(this->monitor    [0]->module        ->s_in1);
		this->duplicator [2]                ->s_out2(this->coset_bit  [0]->module        ->s_in1);
		this->duplicator [0]                ->s_out2(this->encoder    [0]->module        ->s_in );
		this->encoder    [0]->module        ->s_out (this->duplicator [3]                ->s_in );
		this->duplicator [3]                ->s_out1(this->duplicator [4]                ->s_in );
		this->duplicator [4]                ->s_out1(this->coset_real [0]->module        ->s_in1);
		this->duplicator [4]                ->s_out2(this->coset_real_i  ->module        ->s_in1);
		this->duplicator [3]                ->s_out2(this->interleaver_e ->module_inter  ->s_in );
		this->interleaver_e ->module_inter  ->s_out (this->modulator  [0]->module_mod    ->s_in );
		this->modulator  [0]->module_mod    ->s_out (this->channel    [0]->module        ->s_in );
		this->channel    [0]->module        ->s_out (this->modulator  [0]->module_filt   ->s_in );
		this->modulator  [0]->module_filt   ->s_out (this->quantizer  [0]->module        ->s_in );
		this->quantizer  [0]->module        ->s_out (this->modulator  [0]->module_tdemod ->s_in1);
		this->modulator  [0]->module_tdemod ->s_out (this->interleaver[0]->module_deinter->s_in );
		this->interleaver[0]->module_deinter->s_out (this->coset_real [0]->module        ->s_in2);
		this->coset_real [0]->module        ->s_out (this->router                        ->s_in );
		this->router                        ->s_out1(this->siso       [0]->module_siso   ->s_in );
		this->router                        ->s_out2(this->decoder    [0]->module        ->s_in );
		this->siso       [0]->module_siso   ->s_out (this->coset_real_i  ->module        ->s_in2);
		this->coset_real_i  ->module        ->s_out (this->interleaver[0]->module_inter  ->s_in );
		this->interleaver[0]->module_inter  ->s_out (this->modulator  [0]->module_tdemod ->s_in2);
		this->decoder    [0]->module        ->s_out (this->coset_bit  [0]->module        ->s_in2);
		this->coset_bit  [0]->module        ->s_out (this->duplicator [1]                ->s_in );
		this->duplicator [1]                ->s_out1(this->monitor    [0]->module        ->s_in2);
		this->duplicator [1]                ->s_out2(this->predicate                     ->s_in );
	}
	else // standard simulation
	{
		this->source     [0]->module        ->s_out (this->crc        [0]->module        ->s_in );
		this->crc        [0]->module        ->s_out (this->duplicator [0]                ->s_in );
		this->duplicator [0]                ->s_out1(this->monitor    [0]->module        ->s_in1);
		this->duplicator [0]                ->s_out2(this->encoder    [0]->module        ->s_in );
		this->encoder    [0]->module        ->s_out (this->interleaver_e ->module_inter  ->s_in );
		this->interleaver_e ->module_inter  ->s_out (this->modulator  [0]->module_mod    ->s_in );
		this->modulator  [0]->module_mod    ->s_out (this->channel    [0]->module        ->s_in );
		this->channel    [0]->module        ->s_out (this->modulator  [0]->module_filt   ->s_in );
		this->modulator  [0]->module_filt   ->s_out (this->quantizer  [0]->module        ->s_in );
		this->quantizer  [0]->module        ->s_out (this->modulator  [0]->module_tdemod ->s_in1);
		this->modulator  [0]->module_tdemod ->s_out (this->interleaver[0]->module_deinter->s_in );
		this->interleaver[0]->module_deinter->s_out (this->router                        ->s_in );
		this->router                        ->s_out1(this->siso       [0]->module_siso   ->s_in );
		this->router                        ->s_out2(this->decoder    [0]->module        ->s_in );
		this->siso       [0]->module_siso   ->s_out (this->interleaver[0]->module_inter  ->s_in );
		this->interleaver[0]->module_inter  ->s_out (this->modulator  [0]->module_tdemod ->s_in2);
		this->decoder    [0]->module        ->s_out (this->duplicator [1]                ->s_in );
		this->duplicator [1]                ->s_out1(this->monitor    [0]->module        ->s_in2);
		this->duplicator [1]                ->s_out2(this->predicate                     ->s_in );
	}
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::bind_sockets_debug()
{
	if (this->params.code.coset)
	{
		this->source     [0]->module        ->s_out(this->dbg_B[0]->s_in); this->dbg_B[0]->s_out (this->crc        [0]->module        ->s_in );
		this->crc        [0]->module        ->s_out(this->dbg_B[1]->s_in); this->dbg_B[1]->s_out (this->duplicator [0]                ->s_in );
		this->duplicator [0]                                                             ->s_out1(this->duplicator [2]                ->s_in );
		this->duplicator [2]                                                             ->s_out1(this->monitor    [0]->module        ->s_in1);
		this->duplicator [2]                                                             ->s_out2(this->coset_bit  [0]->module        ->s_in1);
		this->duplicator [0]                                                             ->s_out2(this->encoder    [0]->module        ->s_in );
		this->encoder    [0]->module        ->s_out(this->dbg_B[2]->s_in); this->dbg_B[2]->s_out (this->duplicator [3]                ->s_in );
		this->duplicator [3]                                                             ->s_out1(this->duplicator [4]                ->s_in );
		this->duplicator [4]                                                             ->s_out1(this->coset_real [0]->module        ->s_in1);
		this->duplicator [4]                                                             ->s_out2(this->coset_real_i  ->module        ->s_in1);
		this->duplicator [3]                                                             ->s_out2(this->interleaver_e ->module_inter  ->s_in );
		this->interleaver_e ->module_inter  ->s_out(this->dbg_B[3]->s_in); this->dbg_B[3]->s_out (this->modulator  [0]->module_mod    ->s_in );
		this->modulator  [0]->module_mod    ->s_out(this->dbg_R[0]->s_in); this->dbg_R[0]->s_out (this->channel    [0]->module        ->s_in );
		this->channel    [0]->module        ->s_out(this->dbg_R[1]->s_in); this->dbg_R[1]->s_out (this->modulator  [0]->module_filt   ->s_in );
		this->modulator  [0]->module_filt   ->s_out(this->dbg_R[2]->s_in); this->dbg_R[2]->s_out (this->quantizer  [0]->module        ->s_in );
		this->quantizer  [0]->module        ->s_out(this->dbg_Q[0]->s_in); this->dbg_Q[0]->s_out (this->modulator  [0]->module_tdemod ->s_in1);
		this->modulator  [0]->module_tdemod ->s_out(this->dbg_Q[1]->s_in); this->dbg_Q[1]->s_out (this->interleaver[0]->module_deinter->s_in );
		this->interleaver[0]->module_deinter->s_out(this->dbg_Q[2]->s_in); this->dbg_Q[2]->s_out (this->coset_real [0]->module        ->s_in2);
		this->coset_real [0]->module        ->s_out(this->dbg_Q[5]->s_in); this->dbg_Q[5]->s_out (this->router                        ->s_in );
		this->router                                                                     ->s_out1(this->siso       [0]->module_siso   ->s_in );
		this->router                                                                     ->s_out2(this->decoder    [0]->module        ->s_in );
		this->siso       [0]->module_siso   ->s_out(this->dbg_Q[3]->s_in); this->dbg_Q[3]->s_out (this->coset_real_i  ->module        ->s_in2);
		this->coset_real_i  ->module        ->s_out(this->dbg_Q[6]->s_in); this->dbg_Q[6]->s_out (this->interleaver[0]->module_inter  ->s_in );
		this->interleaver[0]->module_inter  ->s_out(this->dbg_Q[4]->s_in); this->dbg_Q[4]->s_out (this->modulator  [0]->module_tdemod ->s_in2);
		this->decoder    [0]->module        ->s_out(this->dbg_B[4]->s_in); this->dbg_B[4]->s_out (this->coset_bit  [0]->module        ->s_in2);
		this->coset_bit  [0]->module        ->s_out(this->dbg_B[5]->s_in); this->dbg_B[5]->s_out (this->duplicator [1]                ->s_in );
		this->duplicator [1]                                                             ->s_out1(this->monitor    [0]->module        ->s_in2);
		this->duplicator [1]                                                             ->s_out2(this->predicate                     ->s_in );
	}
	else // standard simulation
	{
		this->source     [0]->module        ->s_out(this->dbg_B[0]->s_in); this->dbg_B[0]->s_out (this->crc        [0]->module        ->s_in );
		this->crc        [0]->module        ->s_out(this->dbg_B[1]->s_in); this->dbg_B[1]->s_out (this->duplicator [0]                ->s_in );
		this->duplicator [0]                                                             ->s_out1(this->monitor    [0]->module        ->s_in1);
		this->duplicator [0]                                                             ->s_out2(this->encoder    [0]->module        ->s_in );
		this->encoder    [0]->module        ->s_out(this->dbg_B[2]->s_in); this->dbg_B[2]->s_out (this->interleaver_e ->module_inter  ->s_in );
		this->interleaver_e ->module_inter  ->s_out(this->dbg_B[3]->s_in); this->dbg_B[3]->s_out (this->modulator  [0]->module_mod    ->s_in );
		this->modulator  [0]->module_mod    ->s_out(this->dbg_R[0]->s_in); this->dbg_R[0]->s_out (this->channel    [0]->module        ->s_in );
		this->channel    [0]->module        ->s_out(this->dbg_R[1]->s_in); this->dbg_R[1]->s_out (this->modulator  [0]->module_filt   ->s_in );
		this->modulator  [0]->module_filt   ->s_out(this->dbg_R[2]->s_in); this->dbg_R[2]->s_out (this->quantizer  [0]->module        ->s_in );
		this->quantizer  [0]->module        ->s_out(this->dbg_Q[0]->s_in); this->dbg_Q[0]->s_out (this->modulator  [0]->module_tdemod ->s_in1);
		this->modulator  [0]->module_tdemod ->s_out(this->dbg_Q[1]->s_in); this->dbg_Q[1]->s_out (this->interleaver[0]->module_deinter->s_in );
		this->interleaver[0]->module_deinter->s_out(this->dbg_Q[2]->s_in); this->dbg_Q[2]->s_out (this->router                        ->s_in );
		this->router                                                                     ->s_out1(this->siso       [0]->module_siso   ->s_in );
		this->router                                                                     ->s_out2(this->decoder    [0]->module        ->s_in );
		this->siso       [0]->module_siso   ->s_out(this->dbg_Q[3]->s_in); this->dbg_Q[3]->s_out (this->interleaver[0]->module_inter  ->s_in );
		this->interleaver[0]->module_inter  ->s_out(this->dbg_Q[4]->s_in); this->dbg_Q[4]->s_out (this->modulator  [0]->module_tdemod ->s_in2);
		this->decoder    [0]->module        ->s_out(this->dbg_B[4]->s_in); this->dbg_B[4]->s_out (this->duplicator [1]                ->s_in );
		this->duplicator [1]                                                             ->s_out1(this->monitor    [0]->module        ->s_in2);
		this->duplicator [1]                                                             ->s_out2(this->predicate                     ->s_in );
	}
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::terminal_temp_report(Simulation_BFERI<B,R,Q> *simu)
{
	if (!simu->params.terminal.disabled && simu->params.terminal.frequency != std::chrono::nanoseconds(0))
	{
		while (!simu->monitor[0]->fe_limit_achieved() && !simu->monitor[0]->is_interrupt())
		{
			const auto sleep_time = simu->params.terminal.frequency - std::chrono::milliseconds(0);
			std::this_thread::sleep_for(sleep_time);

			// display statistics in terminal
			simu->terminal->temp_report(std::clog);
		}
	}
}

// ---------------------------------------------------------------------------------------------------- virtual methods

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::release_objects()
{
	if (source     [0] != nullptr) { delete source     [0]; source     [0] = nullptr; }
	if (crc        [0] != nullptr) { delete crc        [0]; crc        [0] = nullptr; }
	if (encoder    [0] != nullptr) { delete encoder    [0]; encoder    [0] = nullptr; }
	if (interleaver[0] != nullptr) { delete interleaver[0]; interleaver[0] = nullptr; }
	if (interleaver_e  != nullptr) { delete interleaver_e ; interleaver_e  = nullptr; }
	if (modulator  [0] != nullptr) { delete modulator  [0]; modulator  [0] = nullptr; }
	if (channel    [0] != nullptr) { delete channel    [0]; channel    [0] = nullptr; }
	if (quantizer  [0] != nullptr) { delete quantizer  [0]; quantizer  [0] = nullptr; }
	if (coset_real [0] != nullptr) { delete coset_real [0]; coset_real [0] = nullptr; }
	if (coset_real_i   != nullptr) { delete coset_real_i  ; coset_real_i   = nullptr; }
	if (siso[0] != nullptr)
	{
		// do not delete the siso if the decoder and the siso are the same pointers
		if (decoder[0] == nullptr || siso[0] != dynamic_cast<SISO<Q>*>(decoder[0]))
			delete siso[0];
		siso[0] = nullptr;
	}
	if (decoder    [0] != nullptr) { delete decoder    [0]; decoder    [0] = nullptr; }
	if (coset_bit  [0] != nullptr) { delete coset_bit  [0]; coset_bit  [0] = nullptr; }
	if (monitor    [0] != nullptr) { delete monitor    [0]; monitor    [0] = nullptr; }
	if (terminal       != nullptr) { delete terminal;       terminal       = nullptr; }
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::launch_precompute()
{
}

template <typename B, typename R, typename Q>
void Simulation_BFERI<B,R,Q>
::snr_precompute()
{
}

template <typename B, typename R, typename Q>
Source<B>* Simulation_BFERI<B,R,Q>
::build_source(const int tid)
{
	return Factory_source<B>::build(params, tid);
}

template <typename B, typename R, typename Q>
CRC<B>* Simulation_BFERI<B,R,Q>
::build_crc(const int tid)
{
	return Factory_CRC<B>::build(params);
}

template <typename B, typename R, typename Q>
Encoder<B>* Simulation_BFERI<B,R,Q>
::build_encoder(const int tid)
{
	if (this->params.source.type == "AZCW")
		return Factory_encoder_AZCW<B>::build(params);
	else if (this->params.code.coset)
		return Factory_encoder_coset<B>::build(params, tid);
	else
	{
		std::cerr << bold_red("(EE) The encoder could not be instantiated: try to enable the coset approach or to ")
		          << bold_red("use All Zero Code Words. Exiting...") << std::endl;
		std::exit(-1);
		return nullptr;
	}
}

template <typename B, typename R, typename Q>
Interleaver<int>* Simulation_BFERI<B,R,Q>
::build_interleaver(const int tid)
{
	return Factory_interleaver<int>::build(params, params.code.N + params.code.tail_length, 0);
}

template <typename B, typename R, typename Q>
Modulator<B,R,Q>* Simulation_BFERI<B,R,Q>
::build_modulator(const int tid)
{
	return Factory_modulator<B,R,Q>::build(params, sigma);
}

template <typename B, typename R, typename Q>
Channel<R>* Simulation_BFERI<B,R,Q>
::build_channel(const int size, const int tid)
{
	return Factory_channel<R>::build(params, sigma, size, tid);
}

template <typename B, typename R, typename Q>
Quantizer<R,Q>* Simulation_BFERI<B,R,Q>
::build_quantizer(const int size, const int tid)
{
	return Factory_quantizer<R,Q>::build(params, sigma, size);
}

template <typename B, typename R, typename Q>
Coset<B,Q>* Simulation_BFERI<B,R,Q>
::build_coset_real(const int tid)
{
	return Factory_coset_real<B,Q>::build(params);
}

template <typename B, typename R, typename Q>
Coset<B,B>* Simulation_BFERI<B,R,Q>
::build_coset_bit(const int tid)
{
	return Factory_coset_bit<B>::build(params);
}

template <typename B, typename R, typename Q>
Monitor<B>* Simulation_BFERI<B,R,Q>
::build_monitor(const int tid)
{
	return Factory_monitor<B>::build(params, n_frames);
}

// ------------------------------------------------------------------------------------------------- non-virtual method

template <typename B, typename R, typename Q>
Terminal* Simulation_BFERI<B,R,Q>
::build_terminal(const int tid)
{
	return Factory_terminal<B,R>::build(params, snr, monitor[0], t_snr, d_decod_total_fake);
}

// ==================================================================================== explicit template instantiation 
#include "Tools/types.h"
#ifdef MULTI_PREC
template class Simulation_BFERI<B_8,R_8,Q_8>;
template class Simulation_BFERI<B_16,R_16,Q_16>;
template class Simulation_BFERI<B_32,R_32,Q_32>;
template class Simulation_BFERI<B_64,R_64,Q_64>;
#else
template class Simulation_BFERI<B,R,Q>;
#endif
// ==================================================================================== explicit template instantiation

#endif
