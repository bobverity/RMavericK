
#include "particle.h"
#include "misc.h"
#include "probability.h"
#include "Hungarian.h"

using namespace std;

//------------------------------------------------
// default constructor for particle class
particle::particle() {}

//------------------------------------------------
// constructor for particle class
particle::particle(vector<double> &x, Rcpp::List &args_params, double beta_) {
  
  // extract data and parameters
  x_ptr = &x;
  n = x.size();
  K = rcpp_to_int(args_params["K"]);
  mu_prior_mean = rcpp_to_double(args_params["mu_prior_mean"]);
  mu_prior_var = rcpp_to_double(args_params["mu_prior_var"]);
  sigma = rcpp_to_double(args_params["sigma"]);
  sigma_sq = sigma * sigma;
  beta = beta_;
  scaf_n = rcpp_to_int(args_params["scaf_n"]);
  
  // probabilities and likelihoods
  loglike = 0;
  log_prob_mat = vector<vector<double>>(n, vector<double>(K));
  
  // initialise component means
  mu = vector<double>(K);
  
  // initialise group allocation
  group = vector<int>(n);
  counts = vector<int>(K);
  x_sum = vector<double>(K);
  for (int i=0; i<n; i++) {
    int this_group = sample2(0, K-1);
    group[i] = this_group;
    counts[this_group] ++;
    x_sum[this_group] += x[i];
  }
  
  // initialise Q matrix
  qmatrix = vector<vector<double>>(n, vector<double>(K));
  log_qmatrix = vector<vector<double>>(n, vector<double>(K));
  
  // initialise ordering of labels
  label_order = seq_int(0,K-1);
  label_order_new = vector<int>(K);
  
  // objects for solving label switching problem
  cost_mat = vector<vector<double>>(K, vector<double>(K));
  best_perm = vector<int>(K);
  best_perm_order = vector<int>(K);
  edges_left = vector<int>(K);
  edges_right = vector<int>(K);
  blocked_left = vector<int>(K);
  blocked_right = vector<int>(K);
  
  // initialise scaffold objects
  scaf_group = vector<vector<int>>(scaf_n, vector<int>(n));
  scaf_counts = vector<vector<int>>(scaf_n, vector<int>(K));
  scaf_x_sum = vector<vector<double>>(scaf_n, vector<double>(K));
  
}

//------------------------------------------------
// reset particle
void particle::reset() {
  
  // zero counts etc.
  fill(mu.begin(), mu.end(), 0);
  fill(group.begin(), group.end(), 0);
  fill(counts.begin(), counts.end(), 0);
  fill(x_sum.begin(), x_sum.end(), 0);
  
  // re-draw group allcoation
  for (int i=0; i<n; i++) {
    int this_group = sample2(0, K-1);
    group[i] = this_group;
    counts[this_group] ++;
    x_sum[this_group] += (*x_ptr)[i];
  }
}

//------------------------------------------------
// update component means
void particle::update_mu() {
  for (int k=0; k<K; k++) {
    double mu_post_var = 1/(beta*counts[k]/sigma_sq + 1/mu_prior_var);
    double mu_post_mean = mu_post_var * (beta*x_sum[k]/sigma_sq + mu_prior_mean/mu_prior_var);
    mu[k] = rnorm1(mu_post_mean, sqrt(mu_post_var));
  }
}

//------------------------------------------------
// update group allocations
void particle::update_group() {
  
  // zero counts etc.
  fill(counts.begin(), counts.end(), 0);
  fill(x_sum.begin(), x_sum.end(), 0);
  
  // update all groups
  for (int i=0; i<n; i++) {
    
    // calculate allocation probability and store in Q matrix
    for (int k=0; k<K; k++) {
      log_prob_mat[i][k] = dnorm1((*x_ptr)[i], mu[k], sigma);
    }
    double log_prob_max = max(log_prob_mat[i]);
    double prob_sum = 0;
    for (int k=0; k<K; k++) {
      log_qmatrix[i][k] = beta*(log_prob_mat[i][k] - log_prob_max);
      qmatrix[i][k] = exp(log_qmatrix[i][k]);
      prob_sum += qmatrix[i][k];
    }
    double prob_sum_inv = 1/prob_sum;
    double log_prob_sum = log(prob_sum);
    for (int k=0; k<K; k++) {
      qmatrix[i][k] *= prob_sum_inv;
      log_qmatrix[i][k] -= log_prob_sum;
    }
    
    // draw new group
    group[i] = sample1(qmatrix[i], 1) - 1;
    
    // update counts etc.
    counts[group[i]] ++;
    x_sum[group[i]] += (*x_ptr)[i];
  }
  
}

//------------------------------------------------
// re-order group and other elements such that group is always-increasing
void particle::group_increasing() {
  
  // get order of unique values in group
  vector<int> group_uniques = unique_int(group, K);
  vector<int> group_order = order_unique_int(group_uniques, K);
  
  // zero counts etc.
  fill(counts.begin(), counts.end(), 0);
  fill(x_sum.begin(), x_sum.end(), 0);
  
  // reorder group
  for (int i=0; i<n; i++) {
    int this_group = group_order[group[i]];
    group[i] = this_group;
    counts[this_group] ++;
    x_sum[this_group] += (*x_ptr)[i];
  }
  
  // reorder component means
  vector<double> mu_old = mu;
  for (int k=0; k<K; k++) {
    mu[k] = mu_old[group_uniques[k]];
  }
}

//------------------------------------------------
// return log-probability of a particular scaffold group being drawn
double particle::scaf_prop_logprob(const vector<int> &prop_group) {
  int matches = 0;
  for (int i=0; i<scaf_n; i++) {
    bool exact_match = true;
    for (int j=0; j<n; j++) {
      if (prop_group[j] != scaf_group[i][j]) {
        exact_match = false;
        break;
      }
    }
    if (exact_match) {
      matches ++;
    }
  }
  double ret = log(matches/double(scaf_n));
  return ret;
}

//------------------------------------------------
// propose swap with scaffold grouping
void particle::scaf_propose() {
  
  // re-order group allocation to be always-increasing
  group_increasing();
  
  // initialise backwards proposal probability
  double prop_backwards = scaf_prop_logprob(group);
  
  // break if no chance of backwards move
  if (std::isinf(prop_backwards)) {
    return;
  }
  
  // store old group and propose new group from scaffolds
  vector<int> group_old = group;
  int rand1 = sample2(0, scaf_n-1);
  double prop_forwards = scaf_prop_logprob(scaf_group[rand1]);
  //vector<int> group_new = scaf_group[rand_int1];
  
  // propose new mu
  vector<double> mu_new(K);
  for (int k=0; k<K; k++) {
    double mu_post_var = 1/(beta*scaf_counts[rand1][k]/sigma_sq + 1/mu_prior_var);
    double mu_post_mean = mu_post_var * (beta*scaf_x_sum[rand1][k]/sigma_sq + mu_prior_mean/mu_prior_var);
    double mu_post_sd = sqrt(mu_post_var);
    mu_new[k] = rnorm1(mu_post_mean, mu_post_sd);
    prop_forwards += dnorm1(mu_new[k], mu_post_mean, mu_post_sd);
  }
  
  // calculate backwards probability of drawing this mu
  for (int k=0; k<K; k++) {
    double mu_post_var = 1/(beta*counts[k]/sigma_sq + 1/mu_prior_var);
    double mu_post_mean = mu_post_var * (beta*x_sum[k]/sigma_sq + mu_prior_mean/mu_prior_var);
    double mu_post_sd = sqrt(mu_post_var);
    prop_backwards += dnorm1(mu[k], mu_post_mean, mu_post_sd);
  }
  
  // calculate old and new likelihoods
  double loglike_old = beta*loglike;
  double loglike_new = 0;
  for (int i=0; i<n; i++) {
    loglike_new += dnorm1((*x_ptr)[i], mu_new[scaf_group[rand1][i]], sigma);
  }
  loglike_new *= beta;
  
  // calculate old and new priors
  double logprior_old = 0;
  double logprior_new = 0;
  for (int k=0; k<K; k++) {
    logprior_old += dnorm1(mu[k], mu_prior_mean, sqrt(mu_prior_var));
    logprior_new += dnorm1(mu_new[k], mu_prior_mean, sqrt(mu_prior_var));
  }
  
  // Metropolis-Hastings
  double MH = ((loglike_new + logprior_new) - (loglike_old + logprior_old)) - (prop_forwards - prop_backwards);
  if (log(runif1()) < MH) {
    //accept_jump[rung] <- accept_jump[rung] + 1
    group = scaf_group[rand1];
    counts = scaf_counts[rand1];
    x_sum = scaf_x_sum[rand1];
    mu = mu_new;
  }
}

//------------------------------------------------
// fix label switching problem
void particle::solve_label_switching(const vector<vector<double>> &log_qmatrix_running) {
  
  for (int k1=0; k1<K; k1++) {
    fill(cost_mat[k1].begin(), cost_mat[k1].end(), 0);
    for (int k2=0; k2<K; k2++) {
      for (int i=0; i<n; i++) {
        cost_mat[k1][k2] += qmatrix[i][label_order[k1]]*(log_qmatrix[i][label_order[k1]] - log_qmatrix_running[i][k2]);
      }
    }
  }
  
  // find best permutation of current labels using Hungarian algorithm
  best_perm = hungarian(cost_mat, edges_left, edges_right, blocked_left, blocked_right);
  
  // define best_perm_order
  for (int k=0; k<K; k++) {
    best_perm_order[best_perm[k]] = k;
  }
  
  // replace old label order with new
  for (int k=0; k<K; k++) {
    label_order_new[k] = label_order[best_perm_order[k]];
  }
  label_order = label_order_new;
  
}

//------------------------------------------------
// calculate overall log-likelihood
void particle::calc_log_like() {
  loglike = 0;
  for (int i=0; i<n; i++) {
    loglike += log_prob_mat[i][group[i]];
  }
}
