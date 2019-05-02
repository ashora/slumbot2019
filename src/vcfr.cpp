#include <math.h> // lrint()
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_street_values.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "hand_tree.h"
#include "vcfr_state.h"
#include "vcfr.h"

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

template <>
void VCFR::UpdateRegrets<int>(Node *node, double *vals, shared_ptr<double []> *succ_vals,
			      int *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);

  int floor = regret_floors_[st];
  int ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int *my_regrets = regrets + i * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	// Need different implementation for doubles
	int di = lrint(d * regret_scaling_[st]);
	int ri = my_regrets[s] + di;
	if (ri < floor) {
	  my_regrets[s] = floor;
	} else if (ri > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = ri;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int *my_regrets = regrets + i * num_succs;
      bool overflow = false;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	my_regrets[s] += lrint(d * regret_scaling_[st]);
	if (my_regrets[s] < -2000000000 || my_regrets[s] > 2000000000) {
	  overflow = true;
	}
      }
      if (overflow) {
	for (int s = 0; s < num_succs; ++s) {
	  my_regrets[s] /= 2;
	}
      }
    }
  }
}

// This implementation does not round regrets to ints, nor do scaling.
template <>
void VCFR::UpdateRegrets<double>(Node *node, double *vals, shared_ptr<double []> *succ_vals,
				 double *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  double floor = regret_floors_[st];
  double ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      double *my_regrets = regrets + i * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double newr = my_regrets[s] + succ_vals[s][i] - vals[i];
	if (newr < floor) {
	  my_regrets[s] = floor;
	} else if (newr > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = newr;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      double *my_regrets = regrets + i * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	my_regrets[s] += succ_vals[s][i] - vals[i];
      }
    }
  }
}

// This is ugly, but I can't figure out a better way.
void VCFR::UpdateRegrets(Node *node, int lbd, double *vals, shared_ptr<double []> *succ_vals) {
  int pa = node->PlayerActing();
  int st = node->Street();
  int nt = node->NonterminalID();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  int num_succs = node->NumSuccs();
  CFRStreetValues<double> *d_street_values;
  CFRStreetValues<int> *i_street_values;
  AbstractCFRStreetValues *street_values = regrets_->StreetValues(st);
  if ((d_street_values =
       dynamic_cast<CFRStreetValues<double> *>(street_values))) {
    double *board_regrets = d_street_values->AllValues(pa, nt) +
      lbd * num_hole_card_pairs * num_succs;
    UpdateRegrets(node, vals, succ_vals, board_regrets);
  } else if ((i_street_values =
	      dynamic_cast<CFRStreetValues<int> *>(street_values))) {
    int *board_regrets = i_street_values->AllValues(pa, nt) +
      lbd * num_hole_card_pairs * num_succs;
    UpdateRegrets(node, vals, succ_vals, board_regrets);
  }
}

void VCFR::UpdateRegretsBucketed(Node *node, int *street_buckets, double *vals,
				 shared_ptr<double []> *succ_vals, int *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);

  int floor = regret_floors_[st];
  int ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[i];
      int *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	// Need different implementation for doubles
	int di = lrint(d * regret_scaling_[st]);
	int ri = my_regrets[s] + di;
	if (ri < floor) {
	  my_regrets[s] = floor;
	} else if (ri > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = ri;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[i];
      int *my_regrets = regrets + b * num_succs;
      bool overflow = false;
      for (int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	my_regrets[s] += lrint(d * regret_scaling_[st]);
	if (my_regrets[s] < -2000000000 || my_regrets[s] > 2000000000) {
	  overflow = true;
	}
      }
      if (overflow) {
	for (int s = 0; s < num_succs; ++s) {
	  my_regrets[s] /= 2;
	}
      }
    }
  }
}

// This implementation does not round regrets to ints, nor do scaling.
void VCFR::UpdateRegretsBucketed(Node *node, int *street_buckets, double *vals,
				 shared_ptr<double []> *succ_vals, double *regrets) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  double floor = regret_floors_[st];
  double ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[i];
      double *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	double newr = my_regrets[s] + succ_vals[s][i] - vals[i];
	if (newr < floor) {
	  my_regrets[s] = floor;
	} else if (newr > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = newr;
	}
      }
    }
  } else {
    for (int i = 0; i < num_hole_card_pairs; ++i) {
      int b = street_buckets[i];
      double *my_regrets = regrets + b * num_succs;
      for (int s = 0; s < num_succs; ++s) {
	my_regrets[s] += succ_vals[s][i] - vals[i];
      }
    }
  }
}

void VCFR::UpdateRegretsBucketed(Node *node, int *street_buckets, double *vals,
				 shared_ptr<double []> *succ_vals) {
  int pa = node->PlayerActing();
  int st = node->Street();
  int nt = node->NonterminalID();
  CFRStreetValues<double> *d_street_values;
  CFRStreetValues<int> *i_street_values;
  AbstractCFRStreetValues *street_values = regrets_->StreetValues(st);
  if ((d_street_values =
       dynamic_cast<CFRStreetValues<double> *>(street_values))) {
    double *d_regrets = d_street_values->AllValues(pa, nt);
    UpdateRegretsBucketed(node, street_buckets, vals, succ_vals, d_regrets);
  } else if ((i_street_values =
	      dynamic_cast<CFRStreetValues<int> *>(street_values))) {
    int *i_regrets = i_street_values->AllValues(pa, nt);
    UpdateRegretsBucketed(node, street_buckets, vals, succ_vals, i_regrets);
  }
}

shared_ptr<double []> VCFR::OurChoice(Node *node, int lbd, const VCFRState &state) {
  int pa = node->PlayerActing();
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  int nt = node->NonterminalID();
  shared_ptr<double []> vals;
  unique_ptr< shared_ptr<double []> []> succ_vals(new shared_ptr<double []> [num_succs]);
  for (int s = 0; s < num_succs; ++s) {
    VCFRState succ_state(state, node, s);
    succ_vals[s] = Process(node->IthSucc(s), lbd, succ_state, st);
  }
  if (num_succs == 1) {
    vals = succ_vals[0];
  } else {
    int *street_buckets = state.StreetBuckets(st);
    vals.reset(new double[num_hole_card_pairs]);
    for (int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
    if (best_response_streets_[st]) {
      for (int i = 0; i < num_hole_card_pairs; ++i) {
	double max_val = succ_vals[0][i];
	for (int s = 1; s < num_succs; ++s) {
	  double sv = succ_vals[s][i];
	  if (sv > max_val) {max_val = sv;}
	}
	vals[i] = max_val;
      }
    } else {
      int dsi = node->DefaultSuccIndex();
      bool bucketed = ! buckets_.None(st) &&
	node->LastBetTo() < card_abstraction_.BucketThreshold(st);
      if (bucketed && ! value_calculation_) {
	// This is true when we are running CFR+ on a bucketed system.  We don't want to get the
	// current strategy from the regrets during the iteration, because the regrets for each
	// bucket are in an intermediate state.  Instead we compute the current strategy once at the
	// beginning of each iteration.  current_strategy_ always contains doubles.
	CFRStreetValues<double> *street_values =
	  dynamic_cast< CFRStreetValues<double> *>(
			current_strategy_->StreetValues(st));
	for (int i = 0; i < num_hole_card_pairs; ++i) {
	  int b = street_buckets[i];
	  double *current_probs =
	    street_values->AllValues(pa, nt) + b * num_succs;
	  for (int s = 0; s < num_succs; ++s) {
	    vals[i] += succ_vals[s][i] * current_probs[s];
	  }
	}
      } else {
	AbstractCFRStreetValues *street_values;
	if (value_calculation_) {
	  street_values = sumprobs_->StreetValues(st);
	} else {
	  street_values = regrets_->StreetValues(st);
	}
	unique_ptr<double []> current_probs(new double[num_succs]);
	if (bucketed) {
	  for (int i = 0; i < num_hole_card_pairs; ++i) {
	    int b = street_buckets[i];
	    street_values->RMProbs(pa, nt, b * num_succs, num_succs, dsi, current_probs.get());
	    for (int s = 0; s < num_succs; ++s) {
	      vals[i] += succ_vals[s][i] * current_probs[s];
	    }
	  }
	} else {
	  for (int i = 0; i < num_hole_card_pairs; ++i) {
	    int offset = lbd * num_hole_card_pairs * num_succs +
	      i * num_succs;
	    street_values->RMProbs(pa, nt, offset, num_succs, dsi, current_probs.get());
	    for (int s = 0; s < num_succs; ++s) {
	      vals[i] += succ_vals[s][i] * current_probs[s];
	    }
	  }
	}
      }
      if (! value_calculation_ && ! pre_phase_) {
	if (bucketed) {
	  UpdateRegretsBucketed(node, state.StreetBuckets(st), vals.get(), succ_vals.get());
	} else {
	  // Need values for current board if this is unabstracted system
	  UpdateRegrets(node, lbd, vals.get(), succ_vals.get());
	}
      }
    }
  }

  return vals;
}

shared_ptr<double []> VCFR::OppChoice(Node *node, int lbd, const VCFRState &state) {
  int st = node->Street();
  int num_succs = node->NumSuccs();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  int num_hole_cards = Game::NumCardsForStreet(0);
  int max_card1 = Game::MaxCard() + 1;
  int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;

  const shared_ptr<double []> &opp_probs = state.OppProbs();
  unique_ptr<shared_ptr<double []> []> succ_opp_probs(new shared_ptr<double []> [num_succs]);
  if (num_succs == 1) {
    succ_opp_probs[0].reset(new double[num_enc]);
    for (int i = 0; i < num_enc; ++i) {
      succ_opp_probs[0][i] = opp_probs[i];
    }
  } else {
    int *street_buckets = state.StreetBuckets(st);
    for (int s = 0; s < num_succs; ++s) {
      succ_opp_probs[s].reset(new double[num_enc]);
      for (int i = 0; i < num_enc; ++i) succ_opp_probs[s][i] = 0;
    }

    int dsi = node->DefaultSuccIndex();
    bool bucketed = ! buckets_.None(st) &&
      node->LastBetTo() < card_abstraction_.BucketThreshold(st);

    // At most one of d_sumprob_values and i_sumprob_vals is non-null.  Both may be null.
    CFRStreetValues<double> *d_sumprob_values = nullptr;
    CFRStreetValues<int> *i_sumprob_values = nullptr;
    AbstractCFRStreetValues *sumprob_values = nullptr;
    if (sumprobs_ && sumprob_streets_[st]) {
      sumprob_values = sumprobs_->StreetValues(st);
      if (sumprob_values == nullptr) {
	fprintf(stderr, "No sumprobs values for street %u?!?\n", st);
	exit(-1);
      }
      if ((d_sumprob_values =
	   dynamic_cast<CFRStreetValues<double> *>(sumprob_values)) ==
	  nullptr) {
	if ((i_sumprob_values =
	     dynamic_cast<CFRStreetValues<int> *>(sumprob_values)) ==
	    nullptr) {
	  fprintf(stderr, "Neither int nor double sumprobs?!?\n");
	  exit(-1);
	}
      }
    }

    if (value_calculation_) {
      // For example, RGBR calculation
      if (br_current_) {
	fprintf(stderr, "br_current_ not handled currently\n");
	exit(-1);
      } else {
	if (d_sumprob_values) {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *d_sumprob_values, dsi, it_, soft_warmup_,
			  hard_warmup_, sumprob_scaling_[st], (CFRStreetValues<int> *)nullptr);
	} else if (i_sumprob_values) {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *i_sumprob_values, dsi, it_, soft_warmup_,
			  hard_warmup_, sumprob_scaling_[st], (CFRStreetValues<int> *)nullptr);
	} else {
	  fprintf(stderr, "value_calculation_ and ! br_current_ requires sumprobs\n");
	  exit(-1);
	}
      }
    } else if (bucketed) {
      // This is true when we are running CFR+ on a bucketed system.  We
      // don't want to get the current strategy from the regrets during the
      // iteration, because the regrets for each bucket are in an intermediate
      // state.  Instead we compute the current strategy once at the
      // beginning of each iteration.
      // current_strategy_ always contains doubles
      CFRStreetValues<double> *street_values =
	dynamic_cast< CFRStreetValues<double> *>(current_strategy_->StreetValues(st));
      int nt = node->NonterminalID();
      int pa = node->PlayerActing();
      double *current_probs = street_values->AllValues(pa, nt);
      if (d_sumprob_values) {
	ProcessOppProbs(node, hands, street_buckets, opp_probs.get(), succ_opp_probs.get(),
			current_probs, it_, soft_warmup_, hard_warmup_, sumprob_scaling_[st],
			d_sumprob_values);
      } else {
	ProcessOppProbs(node, hands, street_buckets, opp_probs.get(), succ_opp_probs.get(),
			current_probs, it_, soft_warmup_, hard_warmup_, sumprob_scaling_[st],
			i_sumprob_values);
      }
    } else {
      // Such a mess!
      AbstractCFRStreetValues *cs_values;
      if (value_calculation_ && ! br_current_) {
	// value_calculation_ now handled above
	cs_values = sumprobs_->StreetValues(st);
      } else {
	cs_values = regrets_->StreetValues(st);
      }
      CFRStreetValues<double> *d_cs_values;
      CFRStreetValues<int> *i_cs_values;
      if ((d_cs_values =
	   dynamic_cast<CFRStreetValues<double> *>(cs_values))) {
	if (d_sumprob_values) {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *d_cs_values, dsi, it_, soft_warmup_, hard_warmup_,
			  sumprob_scaling_[st], d_sumprob_values);
	} else {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *d_cs_values, dsi, it_, soft_warmup_, hard_warmup_,
			  sumprob_scaling_[st], i_sumprob_values);
	}
      } else {
	i_cs_values = dynamic_cast<CFRStreetValues<int> *>(cs_values);
	if (i_cs_values == nullptr) {
	  fprintf(stderr, "Neither int nor double cs values?!?\n");
	  exit(-1);
	}
	if (d_sumprob_values) {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *i_cs_values, dsi, it_, soft_warmup_, hard_warmup_,
			  sumprob_scaling_[st], d_sumprob_values);
	} else {
	  ProcessOppProbs(node, lbd, hands, bucketed, street_buckets, opp_probs.get(),
			  succ_opp_probs.get(), *i_cs_values, dsi, it_, soft_warmup_, hard_warmup_,
			  sumprob_scaling_[st], i_sumprob_values);
	}
      }
    }
  }

  shared_ptr<double []> vals;
  double succ_sum_opp_probs;
  for (int s = 0; s < num_succs; ++s) {
    shared_ptr<double []> succ_total_card_probs(new double[max_card1]);
    CommonBetResponseCalcs(st, hands, succ_opp_probs[s].get(), &succ_sum_opp_probs,
			   succ_total_card_probs.get());
    if (prune_ && succ_sum_opp_probs == 0) {
      continue;
    }
    VCFRState succ_state(state, node, s, succ_opp_probs[s], succ_sum_opp_probs,
			 succ_total_card_probs);
    shared_ptr<double []> succ_vals = Process(node->IthSucc(s), lbd, succ_state, st);
    if (vals == nullptr) {
      vals = succ_vals;
    } else {
      for (int i = 0; i < num_hole_card_pairs; ++i) {
	vals[i] += succ_vals[i];
      }
    }
  }
  if (vals == nullptr) {
    // This can happen if there were non-zero opp probs on the prior street,
    // but the board cards just dealt blocked all the opponent hands with
    // non-zero probability.
    vals.reset(new double[num_hole_card_pairs]);
    for (int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  }

  return vals;
}

class VCFRThread {
public:
  VCFRThread(VCFR *vcfr, int thread_index, int num_threads, Node *node, int p,
	     const string &action_sequence, const shared_ptr<double []> &opp_probs,
	     const HandTree *hand_tree, int *prev_canons);
  ~VCFRThread(void) {}
  void Run(void);
  void Join(void);
  void Go(void);
  shared_ptr<double []> RetVals(void) const {return ret_vals_;}
private:
  VCFR *vcfr_;
  int thread_index_;
  int num_threads_;
  Node *node_;
  int p_;
  const string &action_sequence_;
  shared_ptr<double []> opp_probs_;
  const HandTree *hand_tree_;
  int *prev_canons_;
  shared_ptr<double []> ret_vals_;
  pthread_t pthread_id_;
};

VCFRThread::VCFRThread(VCFR *vcfr, int thread_index, int num_threads, Node *node, int p,
		       const string &action_sequence, const shared_ptr<double []> &opp_probs,
		       const HandTree *hand_tree, int *prev_canons) :
  action_sequence_(action_sequence) {
  vcfr_ = vcfr;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
  node_ = node;
  p_ = p;
  opp_probs_ = opp_probs;
  hand_tree_ = hand_tree;
  prev_canons_ = prev_canons;
}

static void *vcfr_thread_run(void *v_t) {
  VCFRThread *t = (VCFRThread *)v_t;
  t->Go();
  return NULL;
}

void VCFRThread::Run(void) {
  pthread_create(&pthread_id_, NULL, vcfr_thread_run, this);
}

void VCFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

void VCFRThread::Go(void) {
  int st = node_->Street();
  int pst = node_->Street() - 1;
  int num_boards = BoardTree::NumBoards(st);
  int num_prev_hole_card_pairs = Game::NumHoleCardPairs(pst);
  Card max_card1 = Game::MaxCard() + 1;
  ret_vals_.reset(new double[num_prev_hole_card_pairs]);
  for (int i = 0; i < num_prev_hole_card_pairs; ++i) ret_vals_[i] = 0;
  for (int bd = thread_index_; bd < num_boards; bd += num_threads_) {
    VCFRState state(p_, opp_probs_, hand_tree_, bd, action_sequence_, 0, 0);
    // Initialize buckets for this street
    vcfr_->SetStreetBuckets(st, bd, state);
    shared_ptr<double []> bd_vals = vcfr_->Process(node_, bd, state, st);
    const CanonicalCards *hands = hand_tree_->Hands(st, bd);
    int board_variants = BoardTree::NumVariants(st, bd);
    int num_hands = hands->NumRaw();
    for (int h = 0; h < num_hands; ++h) {
      const Card *cards = hands->Cards(h);
      Card hi = cards[0];
      Card lo = cards[1];
      int enc = hi * max_card1 + lo;
      int prev_canon = prev_canons_[enc];
      ret_vals_[prev_canon] += board_variants * bd_vals[h];
    }
  }
}

// Divide work at a street-initial node between multiple threads.  Spawns
// the threads, joins them, aggregates the resulting CVs.
// Only support splitting on the flop for now.
// Ugly that we pass prev_canons in.
void VCFR::Split(Node *node, int p, const shared_ptr<double []> &opp_probs,
		 const HandTree *hand_tree, const string &action_sequence, int *prev_canons,
		 double *vals) {
  int nst = node->Street();
  int pst = nst - 1;
  int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  for (int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  unique_ptr<unique_ptr<VCFRThread> []> threads(new unique_ptr<VCFRThread>[num_threads_]);
  for (int t = 0; t < num_threads_; ++t) {
    threads[t].reset(new VCFRThread(this, t, num_threads_, node, p, action_sequence, opp_probs,
				    hand_tree, prev_canons));
  }
  for (int t = 1; t < num_threads_; ++t) {
    threads[t]->Run();
  }
  // Do first thread in main thread
  threads[0]->Go();
  for (int t = 1; t < num_threads_; ++t) {
    threads[t]->Join();
  }
  for (int t = 0; t < num_threads_; ++t) {
    shared_ptr<double []> t_vals = threads[t]->RetVals();
    for (int i = 0; i < prev_num_hole_card_pairs; ++i) {
      vals[i] += t_vals[i];
    }
  }
}

void VCFR::SetStreetBuckets(int st, int gbd, const VCFRState &state) {
  if (buckets_.None(st)) return;
  int num_board_cards = Game::NumBoardCards(st);
  const Card *board = BoardTree::Board(st, gbd);
  Card cards[7];
  for (int i = 0; i < num_board_cards; ++i) {
    cards[i + 2] = board[i];
  }
  int lbd = BoardTree::LocalIndex(state.RootBdSt(), state.RootBd(), st, gbd);

  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  int max_street = Game::MaxStreet();
  int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  int *street_buckets = state.StreetBuckets(st);
  for (int i = 0; i < num_hole_card_pairs; ++i) {
    unsigned int h;
    if (st == max_street) {
      // Hands on final street were reordered by hand strength, but
      // bucket lookup requires the unordered hole card pair index
      const Card *hole_cards = hands->Cards(i);
      cards[0] = hole_cards[0];
      cards[1] = hole_cards[1];
      int hcp = HCPIndex(st, cards);
      h = ((unsigned int)gbd) * ((unsigned int)num_hole_card_pairs) + hcp;
    } else {
      h = ((unsigned int)gbd) * ((unsigned int)num_hole_card_pairs) + i;
    }
    street_buckets[i] = buckets_.Bucket(st, h);
  }
}

shared_ptr<double []> VCFR::StreetInitial(Node *node, int plbd, const VCFRState &state) {
  int nst = node->Street();
  int pst = nst - 1;
  int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
#if 0
  // Move this to CFRP class
  if (nst == subgame_street_ && ! subgame_) {
    if (pre_phase_) {
      SpawnSubgame(node, plbd, state.ActionSequence(), state.OppProbs());
      // Code expects values to be returned so we return all zeroes
      shared_ptr<double []> vals(new double[prev_num_hole_card_pairs]);
      for (int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
      return vals;
    } else {
      int p = node->PlayerActing();
      int nt = node->NonterminalID();
      double *final_vals = final_vals_[p][nt][plbd];
      if (final_vals == nullptr) {
	fprintf(stderr, "No final vals for %u %u %u?!?\n", p, nt, plbd);
	exit(-1);
      }
      final_vals_[p][nt][plbd] = nullptr;
      return final_vals;
    }
  }
#endif
  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *pred_hands = hand_tree->Hands(pst, plbd);
  Card max_card = Game::MaxCard();
  int num_encodings = (max_card + 1) * (max_card + 1);
  unique_ptr<int []> prev_canons(new int[num_encodings]);
  shared_ptr<double []> vals(new double[prev_num_hole_card_pairs]);
  for (int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  for (int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      prev_canons[prev_encoding] = ph;
    }
  }
  for (int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_encoding] = pc;
    }
  }

  // Move this to CFRP class?
  if (nst == 1 && subgame_street_ == -1 && num_threads_ > 1) {
    // Currently only flop supported
    Split(node, state.P(), state.OppProbs(), state.GetHandTree(), state.ActionSequence(),
	  prev_canons.get(), vals.get());
  } else {
    int pgbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), pst, plbd);
    int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
    int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
    for (int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      int nlbd = BoardTree::LocalIndex(state.RootBdSt(),
						state.RootBd(), nst, ngbd);

      const CanonicalCards *hands = hand_tree->Hands(nst, nlbd);
    
      SetStreetBuckets(nst, ngbd, state);
      // I can pass unset values for sum_opp_probs and total_card_probs.  I
      // know I will come across an opp choice node before getting to a terminal
      // node.
      shared_ptr<double []> next_vals = Process(node, nlbd, state, nst);

      int board_variants = BoardTree::NumVariants(nst, ngbd);
      int num_next_hands = hands->NumRaw();
      for (int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	int encoding = hi * (max_card + 1) + lo;
	int prev_canon = prev_canons[encoding];
	vals[prev_canon] += board_variants * next_vals[nh];
      }
    }
  }
  
  // Scale down the values of the previous-street canonical hands
  double scale_down = Game::StreetPermutations(nst);
  for (int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      // Is this doing the right thing?
      vals[ph] /= scale_down * prev_hand_variants;
    }
  }
  // Copy the canonical hand values to the non-canonical
  for (int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      vals[ph] = vals[prev_canons[pred_hands->Canon(ph)]];
    }
  }

  return vals;
}

shared_ptr<double []> VCFR::Process(Node *node, int lbd, const VCFRState &state, int last_st) {
  int st = node->Street();
  if (node->Terminal()) {
    if (node->NumRemaining() == 1) {
      return Fold(node, state.P(), state.GetHandTree()->Hands(st, lbd), state.OppProbs().get(),
		  state.SumOppProbs(), state.TotalCardProbs().get());
    } else {
      return Showdown(node, state.GetHandTree()->Hands(st, lbd), state.OppProbs().get(),
		      state.SumOppProbs(), state.TotalCardProbs().get());
    }
  }
  if (st > last_st) {
    return StreetInitial(node, lbd, state);
  }
  if (node->PlayerActing() == state.P()) {
    return OurChoice(node, lbd, state);
  } else {
    return OppChoice(node, lbd, state);
  }
}

void VCFR::SetCurrentStrategy(Node *node) {
  if (node->Terminal()) return;
  int num_succs = node->NumSuccs();
  int st = node->Street();
  int nt = node->NonterminalID();
  int dsi = node->DefaultSuccIndex();
  int p = node->PlayerActing();

  // In RGBR calculation, for example, only want to set for opp
  if (current_strategy_->StreetValues(st)->Players(p) && ! buckets_.None(st) &&
      node->LastBetTo() < card_abstraction_.BucketThreshold(st) &&
      num_succs > 1) {
    int num_buckets = buckets_.NumBuckets(st);
    AbstractCFRStreetValues *street_values;
    if (value_calculation_ && ! br_current_) {
      street_values = sumprobs_->StreetValues(st);
    } else {
      street_values = regrets_->StreetValues(st);
    }
    CFRStreetValues<double> *d_current_strategy_vals =
      dynamic_cast<CFRStreetValues<double> *>(
	       current_strategy_->StreetValues(st));
    unique_ptr<double []> current_probs(new double[num_succs]);
    for (int b = 0; b < num_buckets; ++b) {
      street_values->RMProbs(p, nt, b * num_succs, num_succs, dsi,
			     current_probs.get());
      d_current_strategy_vals->Set(p, nt, b, num_succs, current_probs.get());
    }
  }
  for (int s = 0; s < num_succs; ++s) {
    SetCurrentStrategy(node->IthSucc(s));
  }
}

VCFR::VCFR(const CardAbstraction &ca, const BettingAbstraction &ba, const CFRConfig &cc,
	   const Buckets &buckets, int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets) {
  num_threads_ = num_threads;
  subgame_street_ = cfr_config_.SubgameStreet();
  soft_warmup_ = cfr_config_.SoftWarmup();
  hard_warmup_ = cfr_config_.HardWarmup();
  nn_regrets_ = cfr_config_.NNR();
  br_current_ = false;
  prob_method_ = ProbMethod::REGRET_MATCHING;
  value_calculation_ = false;
  // Whether we prune branches if no opponent hand reaches.  Normally true,
  // but false when calculating CBRs.
  prune_ = true;
  pre_phase_ = false;

  int max_street = Game::MaxStreet();
  best_response_streets_.reset(new bool[max_street + 1]);
  for (int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  
  sumprob_streets_.reset(new bool[max_street + 1]);
  const vector<int> &ssv = cfr_config_.SumprobStreets();
  int num_ssv = ssv.size();
  if (num_ssv == 0) {
    for (int st = 0; st <= max_street; ++st) {
      sumprob_streets_[st] = true;
    }
  } else {
    for (int st = 0; st <= max_street; ++st) {
      sumprob_streets_[st] = false;
    }
    for (int i = 0; i < num_ssv; ++i) {
      int st = ssv[i];
      sumprob_streets_[st] = true;
    }
  }

  regret_floors_.reset(new int[max_street + 1]);
  const vector<int> &fv = cfr_config_.RegretFloors();
  if (fv.size() == 0) {
    for (int s = 0; s <= max_street; ++s) {
      regret_floors_[s] = 0;
    }
  } else {
    if ((int)fv.size() < max_street + 1) {
      fprintf(stderr, "Regret floor vector too small\n");
      exit(-1);
    }
    for (int s = 0; s <= max_street; ++s) {
      if (fv[s] == 1) regret_floors_[s] = kMinInt;
      else            regret_floors_[s] = fv[s];
    }
  }

  regret_ceilings_.reset(new int[max_street + 1]);
  const vector<int> &cv = cfr_config_.RegretCeilings();
  if (cv.size() == 0) {
    for (int s = 0; s <= max_street; ++s) {
      regret_ceilings_[s] = kMaxInt;
    }
  } else {
    if ((int)cv.size() < max_street + 1) {
      fprintf(stderr, "Regret ceiling vector too small\n");
      exit(-1);
    }
    for (int s = 0; s <= max_street; ++s) {
      if (cv[s] == 0) regret_ceilings_[s] = kMaxInt;
      else            regret_ceilings_[s] = cv[s];
    }
  }

  regret_scaling_.reset(new double[max_street + 1]);
  sumprob_scaling_.reset(new double[max_street + 1]);
  const vector<double> &rv = cfr_config_.RegretScaling();
  if (rv.size() == 0) {
    for (int s = 0; s <= max_street; ++s) {
      regret_scaling_[s] = 1.0;
    }
  } else {
    if ((int)rv.size() < max_street + 1) {
      fprintf(stderr, "Regret scaling vector too small\n");
      exit(-1);
    }
    for (int s = 0; s <= max_street; ++s) {
      regret_scaling_[s] = rv[s];
    }
  }
  const vector<double> &sv = cfr_config_.SumprobScaling();
  if (sv.size() == 0) {
    for (int s = 0; s <= max_street; ++s) {
      sumprob_scaling_[s] = 1.0;
    }
  } else {
    if ((int)sv.size() < max_street + 1) {
      fprintf(stderr, "Sumprob scaling vector too small\n");
      exit(-1);
    }
    for (int s = 0; s <= max_street; ++s) {
      sumprob_scaling_[s] = sv[s];
    }
  }
}
