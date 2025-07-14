<?php
// --- Configuration ---
// IMPORTANT: In a real production environment, use a more secure password
// and load it from a configuration file outside the web root.
$correct_password = "CHANGEME_A_VERY_SECURE_PASSWORD";
$target_dir = "/var/www/ebski.co/firmware/"; // The destination directory on the server

// --- Helper Functions ---
function display_message($message, $is_error = false) {
    $color = $is_error ? 'red' : 'green';
    echo "<!DOCTYPE html><html><head><title>Upload Status</title>";
    echo "<style>body { font-family: Arial, sans-serif; margin: 20px; } .message { padding: 15px; border-radius: 5px; color: white; background-color: " . $color . "; max-width: 600px; margin: auto; text-align: center; }</style>";
    echo "</head><body>";
    echo "<div class='message'>" . htmlspecialchars($message) . "</div>";
    echo "<p style='text-align:center; margin-top:20px;'><a href='firmware_upload.html'>Go back to upload page</a></p>";
    echo "</body></html>";
}

// --- Main Logic ---

// 1. Check if the form was submitted
if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    // If not a POST request, do nothing or redirect.
    header("Location: firmware_upload.html");
    exit;
}

// 2. Check for password
$submitted_password = isset($_POST['password']) ? $_POST['password'] : '';
if (empty($submitted_password) || !hash_equals($correct_password, $submitted_password)) {
    display_message("Error: Invalid password.", true);
    exit;
}

// 3. Validate inputs
$firmware_version = isset($_POST['firmware_version']) ? filter_var($_POST['firmware_version'], FILTER_VALIDATE_INT) : false;
if ($firmware_version === false || $firmware_version <= 0) {
    display_message("Error: Invalid firmware version number provided. It must be a positive integer.", true);
    exit;
}

if (!isset($_FILES['firmware_bin']) || $_FILES['firmware_bin']['error'] !== UPLOAD_ERR_OK) {
    display_message("Error: No file uploaded or an error occurred during upload. Code: " . $_FILES['firmware_bin']['error'], true);
    exit;
}

$file_tmp_path = $_FILES['firmware_bin']['tmp_name'];
$file_name = $_FILES['firmware_bin']['name'];
$file_extension = strtolower(pathinfo($file_name, PATHINFO_EXTENSION));

if ($file_extension !== 'bin') {
    display_message("Error: Invalid file type. Please upload a .bin file.", true);
    exit;
}

// 4. Define target paths
$version_file_path = $target_dir . "firmware.version";
$binary_file_path = $target_dir . "firmware.bin";

// 5. Create/Update firmware.version file
if (file_put_contents($version_file_path, $firmware_version) === false) {
    display_message("Error: Could not write to version file at " . htmlspecialchars($version_file_path) . ". Check directory permissions.", true);
    exit;
}

// 6. Move the uploaded .bin file
if (move_uploaded_file($file_tmp_path, $binary_file_path)) {
    // Success
    display_message("Success! Firmware version " . $firmware_version . " has been uploaded successfully.");
} else {
    // Failure
    display_message("Error: Could not move uploaded file to " . htmlspecialchars($binary_file_path) . ". Check directory permissions.", true);
}

?>
