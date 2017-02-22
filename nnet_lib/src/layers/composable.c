#include "nnet_fwd.h"
#include "core/activation_functions.h"
#include "core/convolution.h"
#include "core/matrix_multiply.h"
#include "core/pooling.h"
#include "core/zeropad.h"
#include "utility/utility.h"
#include "composable.h"

#ifdef DMA_MODE
#include "gem5_harness.h"
#endif

void inner_product_layer_hw(float* activations,
                            float* weights,
                            layer_t* layers,
                            int lnum,
                            float* result) {
    grab_matrix_dma(weights, lnum, layers);
    grab_input_activations_dma(activations, lnum, layers);
    MATRIX_MULTIPLY_WITH_BIAS(activations, weights, NUM_TEST_CASES,
                              layers[lnum].input_rows, layers[lnum].input_cols,
                              result);
    store_output_activations_dma(result, lnum, layers);
}

result_buf inner_product_layer_sw(float* activations,
                                  float* weights,
                                  layer_t* layers,
                                  int lnum,
                                  float* result) {
    inner_product_layer_hw(activations, weights, layers, lnum, result);
    return result;
}

void convolution_layer_hw(float* input,
                          float* kernels,
                          layer_t* layers,
                          int lnum,
                          float* result) {
    layer_t curr_layer = layers[lnum];
    grab_matrix_dma(kernels, lnum, layers);
    grab_input_activations_dma(input, lnum, layers);
    convolution2d_no_padding(input, kernels, curr_layer, result);
    store_output_activations_dma(result, lnum, layers);
}

result_buf convolution_layer_sw(float* input,
                                float* kernels,
                                layer_t* layers,
                                int lnum,
                                float* result) {
    layer_t curr_layer = layers[lnum];
    if (curr_layer.c_padding > 0) {
        int padding = (curr_layer.field_size - 1) / 2;
        // TODO: Replace this with a memcpy implementation.
        copy_zeropad(input, curr_layer, padding, result);
        convolution_layer_hw(result, kernels, layers, lnum, input);
        return input;
    }
    convolution_layer_hw(input, kernels, layers, lnum, result);
    return result;
}

void max_pooling_layer_hw(float* input,
                          float* result,
                          layer_t* layers,
                          int lnum) {
    layer_t curr_layer = layers[lnum];
    grab_input_activations_dma(input, lnum, layers);
    max_pooling(input, result, curr_layer);
    store_output_activations_dma(result, lnum, layers);
}

result_buf max_pooling_layer_sw(float* input,
                                layer_t* layers,
                                int lnum,
                                float* result) {
    max_pooling_layer_hw(input, result, layers, lnum);
    return result;
}

result_buf run_layer(float* activations,
                     float* weights,
                     layer_t* layers,
                     int layer_num,
                     float* result,
                     float* sigmoid_table,
                     bool do_activation_func) {
    layer_t curr_layer = layers[layer_num];
    layer_type l_type = curr_layer.type;
    result_buf result_loc = result;

    if (l_type == FC) {
        PRINT_MSG("\nInner product.\n");
        result_loc = inner_product_layer_sw(
                activations, weights, layers, layer_num, result);
    } else if (l_type == CONV) {
        PRINT_MSG("\nConvolution.\n");
        result_loc = convolution_layer_sw(
                activations, weights, layers, layer_num, result);
    } else if (l_type == POOL_MAX) {
        PRINT_MSG("\nmax pooling\n");
        result_loc = max_pooling_layer_sw(
                activations, layers, layer_num, result);
    }

    if (result_loc == activations) {
        PRINT_DEBUG4D(activations, curr_layer.output_rows,
                      curr_layer.output_cols, curr_layer.output_height);
    } else {
        PRINT_DEBUG4D(result, curr_layer.output_rows,
                      curr_layer.output_cols, curr_layer.output_height);
    }

    if (do_activation_func) {
        PRINT_MSG("\nactivation function\n");
        // Pass through activation function
        if (result_loc == activations) {
            activation_fun(activations,
                           curr_layer.output_rows * curr_layer.output_cols *
                                   curr_layer.output_height,
                           sigmoid_table);
        } else {
            activation_fun(result,
                           curr_layer.output_rows * curr_layer.output_cols *
                                   curr_layer.output_height,
                           sigmoid_table);
        }
    }

    if (result_loc == activations) {
        PRINT_DEBUG4D(activations, curr_layer.output_rows,
                      curr_layer.output_cols, curr_layer.output_height);
    } else {
        PRINT_DEBUG4D(result, curr_layer.output_rows,
                      curr_layer.output_cols, curr_layer.output_height);
    }

    return result_loc;
}

// Runs the forward pass of a neural network.
//
// This version loads weights on a per layer basis, and activations are
// ping-ponged between two buffers, input and result.
void nnet_fwd_composable(float* input,
                         float* weights,
                         layer_t* layers,
                         int num_layers,
                         float* result,
                         float* sigmoid_table) {

    int l;
    layer_t curr_layer;

    // Alternate between reading from/writing to input and result so we can
    // avoid copying matrices. The initial input is obviously in "input", so
    // that's where we start.
    result_buf result_loc = input;
    bool do_activation_func = true;

    if (PRINT_DATA_AND_WEIGHTS) {
        print_data_and_weights(input, weights, layers[0]);
    }

    // FORMAT HERE IS H TIMES W, NOT W TIMES H!!!!!
    // SO EACH DATA POINT IS A ***ROW****

    l = 0;

    //******************//
    //   PRIMARY LOOP   //
    //******************//

nnet_fwd_outer:
    for (l = 0; l < num_layers; l++) {
        curr_layer = layers[l];
        // Don't run the activation function on the last layer.
        do_activation_func = (l != num_layers - 1);

        if (result_loc == result) {
            result_loc = run_layer(result, weights, layers, l, input,
                                   sigmoid_table, do_activation_func);
        } else {
            result_loc = run_layer(input, weights, layers, l, result,
                                   sigmoid_table, do_activation_func);
        }
    }

    layers[num_layers - 1].result_in_temp = result_loc == result;

    if (result_loc == result)
        dmaStore(result, 0, 0, NUM_TEST_CASES * NUM_CLASSES * sizeof(float));
    else
        dmaStore(input, 0, 0, NUM_TEST_CASES * NUM_CLASSES * sizeof(float));
    dmaStore(layers, 0, 0, num_layers*sizeof(layer_t));
}
