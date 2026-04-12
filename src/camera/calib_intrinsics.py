#!/usr/bin/env python3
import cv2
import numpy as np
import glob
import argparse
import yaml

def main():
    parser = argparse.ArgumentParser(description="Camera calibration using checkerboard images.")
    parser.add_argument("--dir", type=str, required=True, help="Directory with calibration images")
    parser.add_argument("--rows", type=int, required=True, help="Checkerboard inner corners rows")
    parser.add_argument("--cols", type=int, required=True, help="Checkerboard inner corners cols")
    parser.add_argument("--size", type=float, required=True, help="Square size in meters")
    args = parser.parse_args()

    # termination criteria
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)

    # prepare object points
    objp = np.zeros((args.rows * args.cols, 3), np.float32)
    objp[:, :2] = np.mgrid[0:args.cols, 0:args.rows].T.reshape(-1, 2)
    objp *= args.size

    objpoints = []  # 3d points
    imgpoints = []  # 2d points

    images = sorted(glob.glob(args.dir + "/*.jpg") +
                    glob.glob(args.dir + "/*.png") +
                    glob.glob(args.dir + "/*.jpeg"))

    if len(images) == 0:
        print("Keine Bilder gefunden.")
        return

    print(f"{len(images)} Bilder gefunden.")

    for fname in images:
        img = cv2.imread(fname)
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

        ret, corners = cv2.findChessboardCorners(gray, (args.cols, args.rows), None)

        if ret:
            corners2 = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)
            imgpoints.append(corners2)
            objpoints.append(objp)
            print(f"✔ Checkerboard gefunden in: {fname}")
        else:
            print(f"✖ Kein Checkerboard in: {fname}")

    print("\n🔧 Kalibriere Kamera…")

    ret, cameraMatrix, distCoeffs, rvecs, tvecs = cv2.calibrateCamera(
        objpoints, imgpoints, gray.shape[::-1], None, None
    )

    print("\nFERTIG!")
    print("Reprojection Error:", ret)
    print("\ncameraMatrix:\n", cameraMatrix)
    print("\ndistCoeffs:\n", distCoeffs)

    data = {
        "cameraMatrix": cameraMatrix.tolist(),
        "distCoeffs": distCoeffs.tolist(),
        "reprojection_error": float(ret)
    }

    with open("calibration.yaml", "w") as f:
        yaml.dump(data, f)

    print("\n✔ calibration.yaml wurde gespeichert.")

if __name__ == "__main__":
    main()
