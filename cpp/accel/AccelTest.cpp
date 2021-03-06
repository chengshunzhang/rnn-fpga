#include "AccelTest.h"
#include "AccelSchedule.h"
#include <math.h>
#include <cstdlib>

//------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------

bool layer_is_rnn(unsigned layer_idx) {
  assert(layer_idx != 0 && layer_idx <= N_LAYERS);
  return T_tab[layer_idx-1] == LAYER_RNN1 || T_tab[layer_idx-1] == LAYER_RNN2;
}

bool layer_is_last(unsigned layer_idx) {
  assert(layer_idx != 0 && layer_idx <= N_LAYERS);
  return T_tab[layer_idx-1] == LAYER_LAST;
}

// Simple log function, only works for powers of 2
unsigned log2(unsigned x) {
  unsigned res = 0;
  while (x != 1) {
    x = x >> 1;
    res += 1;
  }
  return res;
}

//------------------------------------------------------------------------
// Binarize weights and pack them into Words
//------------------------------------------------------------------------

void set_rnn_weight_array(Word* w, const float* wts_in, const float* wts_hid, unsigned layer_idx, unsigned weight_idx) {
  const unsigned M = M_tab[layer_idx-1];
  const unsigned N = N_tab[layer_idx-1];
  unsigned w_idx = 0;
  for (unsigned n = 0; n < N; ++n) {
    for (unsigned m = 0; m < M + N; m+=WORD_SIZE) {
      Word wrd = 0;
      if (m < M) {
        for (unsigned b = 0; b < WORD_SIZE; ++b) {
          wrd[b] = ((wts_in[(m+b)*N+n] < 0) ? 0 : 1);
        }
      } else {
        for (unsigned b = 0; b < WORD_SIZE; ++b) {
          wrd[b] = ((wts_hid[(m-M+b)*N+n] < 0) ? 0 : 1);
        }
      }
      w[weight_idx*(M+N)*N/WORD_SIZE + w_idx] = wrd;
      ++w_idx;
    }
  }
}

void set_rnn_bias_array(Word* b, const float* bias, unsigned layer_idx, unsigned weight_idx) {
  const unsigned N = N_tab[layer_idx-1];
  unsigned b_idx = 0;
  Word wrd = 0;
  for (unsigned n = 0; n < N; n+=WORD_SIZE) {
    for (unsigned b = 0; b < WORD_SIZE; ++b) {
      wrd[b] = ((bias[n + b] < 0) ? 0 : 1);
    }
    b[weight_idx*N/WORD_SIZE + b_idx] = wrd;
    ++b_idx;
  }
}



void set_dense_weight_array(Word* w, const float* wts, unsigned layer_idx) {
  const unsigned M = M_tab[layer_idx-1];
  const unsigned N = N_tab[layer_idx-1];
  unsigned w_idx = 0;
  for (unsigned n = 0; n < N; ++n) {
    for (unsigned m = 0; m < M; m+=WORD_SIZE) {
      Word wrd = 0;
      for (unsigned b = 0; b < WORD_SIZE; ++b) {
        wrd[b] = ((wts[(m+b)*N+n] < 0) ? 0 : 1);
      }   
      w[w_idx] = wrd;
      ++w_idx;
    }
  }
}

void set_dense_bias_array(Word* b, const float* bias, unsigned layer_idx) {
  const unsigned N = N_tab[layer_idx-1];
  unsigned b_idx = 0;
  Word wrd = 0;
  for (unsigned n = 0; n < N; n+= WORD_SIZE) {
    for (unsigned b = 0; b < WORD_SIZE; ++b) {
      wrd[b] = ((bias[n + b] < 0) ? 0 : 1);
    }
    b[b_idx] = wrd;
    ++b_idx;
  }
}


/*
//------------------------------------------------------------------------
// Helper test function for the accelerator conv layers
//------------------------------------------------------------------------
void test_conv_layer(
    Word* weights,
    Word* kh,
    Word* data_i,
    Word* data_o,
    Word* conv_ref,
    Word* bin_ref,
    const unsigned M,
    const unsigned N,
    const unsigned Si,
    const ap_uint<1> conv_mode, // 0=conv1, 1=conv
    const ap_uint<1> max_pool
) {
  printf ("#### Testing convolution with %u inputs, width %u ####\n", M, Si);
  unsigned So = max_pool ? Si/2 : Si;
  unsigned input_words = conv_mode==0 ? Si*Si : M*Si*Si/WORD_SIZE;
  unsigned output_words = N*So*So/WORD_SIZE;
  if (output_words < 1) output_words = 1;
  assert (input_words <= DMEM_WORDS);
  //assert (output_words <= DMEM_O_WORDS);

  DB(3,
    printf ("*data*:\n");
    print_bits3d(data_i, 0, 1, Si, 6,Si);
    printf ("*params*:\n");
    print_params3d(weights, 0, 15);
  )

  AccelSchedule sched;
  compute_accel_schedule(
      weights, kh,
      M, N, Si,
      conv_mode.to_int(),
      max_pool,
      sched
  );

  run_accel_schedule(
      data_i, data_o,
      0,      // layer_idx
      input_words,
      output_words,
      0,      // dmem_mode
      sched
  );

  // print results
  printf ("*bin out*:\n");
  print_bits3d(data_o, 0, 1, So, 8,So);
  printf ("*bin ref*:\n");
  print_bits3d(bin_ref, 0, 1, So, 8,So);

  // Compare bin results
  printf ("## Checking results ##\n");
  unsigned n_err = 0;
  for (unsigned n = 0; n < N; ++n) {
    for (unsigned r = 0; r < So; ++r) {
      for (unsigned c = 0; c < So; ++c) {
        if (get_bit(data_o, n*So*So+r*So+c) != get_bit(bin_ref, n*So*So+r*So+c)) {
          n_err++;
          //printf ("bin out != ref at n=%d, (%d,%d)\n", n, r,c);
          //if (n_err > 64) exit(-1);
        }
      }
    }
  }
  float err_rate = float(n_err) / (N*So*So)*100;
  printf ("Error rate: %7.4f%%\n", err_rate);
  assert(err_rate < 1.0);
}

//------------------------------------------------------------------------
// Helper test function for the accelerator dense layers
//------------------------------------------------------------------------
void test_dense_layer(
    Word* weights,
    Word* kh,
    Word* data_i,
    Word* data_o,
    Word* bin_ref,
    const unsigned M,   // pixels
    const unsigned N    // pixels
) {
  printf ("#### Testing dense layer with %u inputs, %u outputs ####\n", M, N);
  DB(3,
    printf ("*data*:\n");
    print_bits(data_i, 0, 16, 8, 16);
    printf ("*params*:\n");
    print_bits(weights, 0, 16, 8, 16);
  )

  AccelSchedule sched;
  compute_accel_schedule(
      weights, kh,
      M, N, 1,
      2,      // layer_mode
      0,      // norm_mode
      sched
  );

  run_accel_schedule(
      data_i, data_o,
      0,      // layer_idx
      M/WORD_SIZE,
      N/WORD_SIZE,
      0,      // dmem_mode
      sched
  );

  // print results
  printf ("*bin out*:\n");
  print_bits(data_o, 0, 16, 8, 16);
  printf ("*bin ref*:\n");
  print_bits(bin_ref, 0, 16, 8, 16);

  // Compare bin results
  printf ("## Checking results ##\n");
  unsigned n_err = 0;
  for (unsigned n = 0; n < N; ++n) {
    if (get_bit(data_o, n) != get_bit(bin_ref, n)) {
      n_err++;
    }
  }
  float err_rate = float(n_err)/N * 100;
  printf ("Error rate: %7.4f%%\n", err_rate);
  assert(err_rate < 1.0);
}*/
