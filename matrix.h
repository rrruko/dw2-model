typedef struct vertex_s {
  int16_t x;
  int16_t y;
  int16_t z;
} vertex_t;

typedef struct matrix_s {
  int16_t x[9];
} matrix_t;

typedef struct fmatrix_s {
  float x[9];
} fmatrix_t;

typedef struct quaternion_s {
  float w;
  float x;
  float y;
  float z;
} quaternion_t;

vertex_t rotate(matrix_t m, vertex_t v);
vertex_t translate(vertex_t a, vertex_t b);
fmatrix_t matrix_to_fmatrix(matrix_t m);
quaternion_t matrix_to_quaternion(fmatrix_t m);
void display_matrix_debug(matrix_t* m);
void display_quaternion_debug(quaternion_t* q);
void normalize_quaternion_inplace(quaternion_t* q);
void decompose(fmatrix_t m, fmatrix_t* s_out, fmatrix_t* r_out);
