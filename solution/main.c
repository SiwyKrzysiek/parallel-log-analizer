#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "mpiUtils.h"
#include "logEntry.h"
#include "./libs/c_hashmap/hashmap.h"
#include "./libs/c-vector/cvector.h"

#define CVECTOR_LOGARITHMIC_GROWTH
#define master 0

#define LOG_FILE_NAME "fragment.log"

void mpiClenup()
{
    MPI_Finalize();
}

int printData(any_t item, any_t data) {
    int value = *(int*) data;
    printf("vlue: %d\n", value);

    return MAP_OK;
}

int main(int argc, char **argv)
{
    // Initialize the MPI environment
    MPI_Init(&argc, &argv);
    atexit(mpiClenup);
    // TODO: Get selectedField from rguments
    const char* selectedField = "addr";

    // Get the number of all processes and rank of current process
    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    cvector_vector_type(char *) loadedFields = NULL;

    // Load selected fields from file
    if (world_rank == master) {
        FILE* file = fopen(LOG_FILE_NAME, "r");
        if (!file) {
            fprintf(stderr, "Unable to open read file\n");
            exit(EXIT_FAILURE);
        }

        char line[FIELD_SIZE * 10];
        while(fgets(line, sizeof(line), file)) {
            LogEnrtry entry = parseLogEntry(line);
            char *field = malloc(FIELD_SIZE * sizeof(*field));

            if (getFieldFromLogEntry(&entry, selectedField, field) != 0) {
                fprintf(stderr, "Wrong field name\n");
                for (int i = 0; i < cvector_size(loadedFields); i++)
                {
                    free(loadedFields[i]);
                }
                cvector_free(loadedFields);
                fclose(file);
                exit(EXIT_FAILURE);
            }

            cvector_push_back(loadedFields, field);
        }
        fclose(file);
    }

    // Prepare fo Scatterv
    int *sizes;
    int *skips;
    if (world_rank == master)
    {
        sizes = malloc(world_size * sizeof(*sizes));
        skips = malloc(world_size * sizeof(*skips));
        prepareDataForAsymetricOperaton(world_size, cvector_size(loadedFields), sizes, skips);
    }

    // Create data type for log field
    MPI_Datatype dt_field;
    MPI_Type_contiguous(FIELD_SIZE, MPI_CHAR, &dt_field);
    MPI_Type_commit(&dt_field);

    // if (world_rank == master)
    // {
    //     printf("Data before scatter\n");
    //     for (int i = 0; i < cvector_size(loadedFields); i++)
    //     {
    //         printf("%s\n", loadedFields[i]);
    //     }
    // }

    int mySize;
    MPI_Scatter(sizes, 1, MPI_INT, &mySize, 1, MPI_INT, master, MPI_COMM_WORLD);
    printf("%d -> size: %d\n", world_rank, mySize);
    char myPart[mySize][FIELD_SIZE];

    // if (world_rank == master)
    // {
    //     for (int i = 0; i < world_size; i++)
    //     {
    //         printf("%d :-> size: %d skip: %d\n", i, sizes[i], skips[i]);
    //     }
    // }

    // Loaded data needs to be flattened before scatter
    char **flattendedData = NULL;
    if (world_rank == master)
    {
        flattendedData = malloc(cvector_size(loadedFields) * FIELD_SIZE * sizeof(*loadedFields));
        char* p = (char*) flattendedData;
        for (int i = 0; i < cvector_size(loadedFields); i++)
        {
            memcpy(p, loadedFields[i], FIELD_SIZE * sizeof(*loadedFields));
            p += FIELD_SIZE;
            free(loadedFields[i]); // Now vector can be freed
        }
        cvector_free(loadedFields);
    }

    // Scatter data
    MPI_Scatterv(flattendedData, sizes, skips, dt_field, myPart, mySize, dt_field, master, MPI_COMM_WORLD);
    free(flattendedData);

    for (int i = 0; i < mySize; i++)
    {
        // if (world_rank == master)
        printf("%d -> myPart[%d] = %s\n", world_rank, i, myPart[i]);
    }

    // Free resources
    if (world_rank == master) {
        free(sizes);
        free(skips);
    }

    MPI_Type_free(&dt_field);
    return EXIT_SUCCESS;
}