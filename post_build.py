import re
import shutil
import os

Import('env')

# --- Configuration ---
# Get the source code path from the environment
# Note: This assumes a single source file in the `src` dir of the environment.
# A more robust solution might be needed for complex projects, but this is fine for now.
source_code_path = os.path.join(env.get("PROJECT_SRC_DIR"), "esp32devkitv1", "main.cpp")
output_dir = "build_output"

# --- Functions ---
def get_current_version(file_path):
    """Reads the version from the specified C++ source file."""
    try:
        with open(file_path, "r") as f:
            content = f.read()
            match = re.search(r"const int FIRMWARE_VERSION = (\d+);", content)
            if match:
                return int(match.group(1))
    except IOError as e:
        print(f"Error reading source file: {e}")
    return None

def create_build_files(source, target, env):
    """This function is called after a successful build."""
    print("--- Running Post-Build Script ---")

    # Get the path to the compiled binary
    firmware_path = str(target[0])

    version = get_current_version(source_code_path)
    if version is None:
        print(f"Error: Could not find FIRMWARE_VERSION in {source_code_path}")
        return

    print(f"Firmware Version: {version}")

    # Create output directory if it doesn't exist
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Create version file
    version_file_path = os.path.join(output_dir, "firmware.version")
    with open(version_file_path, "w") as f:
        f.write(str(version))
    print(f"Created {version_file_path}")

    # Copy binary file
    binary_file_path = os.path.join(output_dir, "firmware.bin")
    shutil.copy(firmware_path, binary_file_path)
    print(f"Copied {firmware_path} to {binary_file_path}")
    print("--- Post-Build Script Finished ---")


# --- Hook into the build process ---
# The AddPostAction function registers a function to be called after the target is built.
# "$PROGPATH" is a PlatformIO variable that represents the final firmware binary.
env.AddPostAction("$PROGPATH", create_build_files)
