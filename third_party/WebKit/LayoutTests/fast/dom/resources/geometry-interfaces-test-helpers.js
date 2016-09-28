function assert_identity_2d_matrix(actual) {
  assert_true(actual instanceof DOMMatrixReadOnly);
  assert_true(actual.is2D, "is2D");
  assert_true(actual.isIdentity, "isIdentity");
  assert_identity_matrix(actual);
}

function assert_identity_3d_matrix(actual) {
  assert_true(actual instanceof DOMMatrixReadOnly);
  assert_false(actual.is2D, "is2D");
  assert_true(actual.isIdentity, "isIdentity");
  assert_identity_matrix(actual);
}

function assert_identity_matrix(actual) {
  assert_array_equals(actual.toFloat64Array(), [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
}

function toArray(actual) {
  var array = actual.toFloat64Array();
  // Do not care negative zero for testing accomodation.
  for (var i = 0; i < array.length; i++) {
    if (array[i] === -0)
      array[i] = 0;
  }
  return array;
}

function assert_2d_matrix_equals(actual, expected) {
  assert_true(actual instanceof DOMMatrixReadOnly);
  assert_true(Array.isArray(expected));
  assert_equals(6, expected.length, "expected.length");
  assert_true(actual.is2D, "is2D");
  assert_false(actual.isIdentity, "isIdentity");
  assert_array_equals(toArray(actual), [
    expected[0], expected[1], 0, 0,
    expected[2], expected[3], 0, 0,
    0, 0, 1, 0,
    expected[4], expected[5], 0, 1
  ]);
}

function assert_3d_matrix_equals(actual, expected) {
  assert_true(actual instanceof DOMMatrixReadOnly);
  assert_true(Array.isArray(expected) );
  assert_equals(16, expected.length, "expected.length");
  assert_false(actual.is2D, "is2D");
  assert_false(actual.isIdentity, "isIdentity");
  assert_array_equals(toArray(actual), [
    expected[0], expected[1], expected[2], expected[3],
    expected[4], expected[5], expected[6], expected[7],
    expected[8], expected[9], expected[10], expected[11],
    expected[12], expected[13], expected[14], expected[15],
  ]);
}
