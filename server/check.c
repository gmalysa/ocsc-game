#include <math.h>
#include <stdio.h>

#include <libgjm/errors.h>

#include "game.h"

#define NORMAL_TEST_COUNT 10000000

#define ATTR_TEST_COUNT 10000000

/**
 * To check that something actually is normally distributed we are going to look at
 * the skewness and excess kurtosis which should both be zero for a normal distribution.
 * Of course these are estimated from the data so really the result we want is "small"
 * This doesn't check that they're independent
 */
void check_normals(void) {
	size_t i;
	double *data;
	double mean;
	double vsum;
	double ssum;
	double ksum;

	data = calloc(NORMAL_TEST_COUNT, sizeof(*data));

	for (i = 0; i < NORMAL_TEST_COUNT/2; ++i) {
		get_normals(&data[2*i], &data[2*i+1]);
	}

	mean = 0.0;
	for (i = 0; i < NORMAL_TEST_COUNT; ++i) {
		mean = mean + data[i];
	}
	mean = mean / NORMAL_TEST_COUNT;

	vsum = 0;
	ssum = 0;
	ksum = 0;
	for (i = 0; i < NORMAL_TEST_COUNT; ++i) {
		double adj = data[i] - mean;
		vsum += pow(adj, 2);
		ssum += pow(adj, 3);
		ksum += pow(adj, 4);
	}

	vsum = vsum / NORMAL_TEST_COUNT;
	ssum = ssum / NORMAL_TEST_COUNT;
	ksum = ksum / NORMAL_TEST_COUNT;

	printf("check_normals:\n");
	printf("  mean: %f\n", mean);
	printf("  variance: %f\n", vsum);
	printf("  skewness: %f\n", ssum / pow(vsum, 3./2));
	printf("  excess kurtosis: %f\n", ksum / pow(vsum, 2) - 3);

	free(data);
}

/**
 * Measure the covariance matrix for a set of game parameters
 */
void measure_covariance(int id, struct game_params_t *params) {
	size_t i, j, k;
	size_t n = params->rng_params.n;
	uint32_t *attrs;
	double *vec;
	double *mean;
	double *Q;

	attrs = calloc(ATTR_TEST_COUNT, sizeof(*attrs));
	ASSERT(attrs);

	mean = calloc(n, sizeof(*mean));
	ASSERT(mean);

	vec = calloc(n, sizeof(*vec));
	ASSERT(vec);

	Q = calloc(n*n, sizeof(*Q));
	ASSERT(Q);

	for (i = 0; i < ATTR_TEST_COUNT; ++i) {
		attrs[i] = generate_attributes(params->rng_params.n,
			params->rng_params.t, params->rng_params.a);
	}

	// E[x] for each attribute in mean
	for (i = 0; i < ATTR_TEST_COUNT; ++i) {
		for (j = 0; j < n; ++j) {
			if (is_flag_set(attrs[i], BIT(j)))
				mean[j] += 1;
		}
	}
	for (i = 0; i < n; ++i) {
		mean[i] = mean[i] / ATTR_TEST_COUNT;
	}

	// Covariance matrix estimated using the sample covariance
	// 1/(n-1) sum (X-E[X])(X-E[X])^T
	for (i = 0; i < ATTR_TEST_COUNT; ++i) {
		for (j = 0; j < n; ++j) {
			vec[j] = -mean[j];
			if (is_flag_set(attrs[i], BIT(j)))
				vec[j] += 1.0;
		}

		for (j = 0; j < n; ++j) {
			for (k = 0; k < n; ++k) {
				Q[n*j + k] += vec[j] * vec[k];
			}
		}
	}
	for (j = 0; j < n; ++j) {
		for (k = 0; k < n; ++k) {
			Q[n*j + k] = Q[n*j + k] / (ATTR_TEST_COUNT-1);
		}
	}

	printf("Measured statistics (game %d):\n", id);
	printf("  # of attributes: %zu\n", n);
	printf("\n  means   :");
	for (i = 0; i < n; ++i) {
		printf(" % 08.6f", mean[i]);
	}
	printf("\n");

	printf("  E[means]:");
	for (i = 0; i < n; ++i) {
		printf(" % 08.6f", params->dist_params.marginals[i]);
	}
	printf("\n");

	printf("\n  variances (bernoulli calc):");
	for (i = 0; i < n; ++i) {
		printf("\n    ");
		for (j = 0; j < i; ++j) {
			printf("          ");
		}
		printf("% 08.6f", mean[i]*(1-mean[i]));
	}
	printf("\n");

	printf("  covariance matrix:\n");
	for (j = 0; j < n; ++j) {
		printf("   ");
		for (k = 0; k < n; ++k) {
			printf(" % 08.6f", Q[n*j + k]);
		}
		printf("\n");
	}
	printf("\n");

	printf("  correlation matrix:\n");
	for (j = 0; j < n; ++j) {
		printf("   ");
		for (k = 0; k < n; ++k) {
			printf(" % 08.6f", Q[n*j + k]/sqrt(Q[n*j + j]*Q[n*k + k]));
		}
		printf("\n");
	}

	free(attrs);
}

int main(int argc, char **argv) {
	error_t *ret;

	ret = init_game();
	if (NOT_OK(ret)) {
		error_print(ret);
		exit(1);
	}

	check_normals();
	for (size_t i = 0; i < get_number_of_games(); ++i) {
		printf("\n----\n\n");
		measure_covariance(i, get_game_params(i));
	}

	return 0;
}
