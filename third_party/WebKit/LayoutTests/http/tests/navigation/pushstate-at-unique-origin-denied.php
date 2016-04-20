<?php
header("Content-Security-Policy: sandbox allow-scripts");
?>
<script src="../resources/testharness.js"></script>
<script src="../resources/testharnessreport.js"></script>
<script>
test(function () {
    assert_throws('SecurityError', function () {
        history.pushState(null, null, document.URL + "/path");
    });
}, 'pushState to a new path in unique origin should fail with SecurityError');
test(function () {
    try {
        history.pushState(null, null, document.URL + "#hash");
        done();
    } catch (e) {
        assert_unreached("pushState to a new hash should not fail.");
    }
}, 'pushState to new hash in unique origin should not fail with SecurityError');
</script>
