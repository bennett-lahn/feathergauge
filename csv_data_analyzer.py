#!/usr/bin/env python3
"""
CSV Data Point Analyzer

This script analyzes a CSV file with sensor data to compare the actual number
of data points against the expected number based on the time range and 
sampling frequency.

Expected CSV format:
W.G. Num: 35,Timestamp,Pressure [mbar],Temp [deg C],Battery [VDC]

Example data line:
2025/9/3,14:0:12:412,997.00,26.22,4.29

Usage:
    python csv_data_analyzer.py <csv_file_path> [--sampling-rate <rate>]
"""

import csv
import argparse
import sys
from datetime import datetime
from typing import Tuple, List
import os


def parse_timestamp(timestamp_str: str) -> datetime:
    """
    Parse timestamp string into datetime object.
    Supports multiple common timestamp formats.
    """
    # Common timestamp formats to try
    formats = [
        '%Y/%m/%d %H:%M:%S:%f',   # 2025/9/3 14:1:20:287 (with milliseconds)
        '%Y/%m/%d %H:%M:%S',      # 2025/9/3 14:1:20
        '%Y-%m-%d %H:%M:%S',      # 2023-12-01 14:30:25
        '%Y-%m-%d %H:%M:%S.%f',   # 2023-12-01 14:30:25.123456
        '%Y-%m-%dT%H:%M:%S',      # 2023-12-01T14:30:25
        '%Y-%m-%dT%H:%M:%S.%f',   # 2023-12-01T14:30:25.123456
        '%Y-%m-%dT%H:%M:%SZ',     # 2023-12-01T14:30:25Z
        '%Y-%m-%dT%H:%M:%S.%fZ',  # 2023-12-01T14:30:25.123456Z
        '%m/%d/%Y %H:%M:%S',      # 12/01/2023 14:30:25
        '%d/%m/%Y %H:%M:%S',      # 01/12/2023 14:30:25
    ]
    
    for fmt in formats:
        try:
            return datetime.strptime(timestamp_str.strip(), fmt)
        except ValueError:
            continue
    
    # Special handling for format: 2025/9/3,14:0:12:412 (date and time separated by comma)
    try:
        timestamp_str = timestamp_str.strip()
        if ',' in timestamp_str and ':' in timestamp_str:
            # Split by comma to separate date and time
            date_part, time_part = timestamp_str.split(',', 1)
            # Replace the last colon (milliseconds separator) with a period
            time_parts = time_part.split(':')
            if len(time_parts) == 4:  # H:M:S:ms
                time_part = ':'.join(time_parts[:3]) + '.' + time_parts[3]
                # Pad milliseconds to 6 digits if needed
                if '.' in time_part:
                    base_time, ms = time_part.split('.')
                    ms = ms.ljust(6, '0')[:6]  # Pad to 6 digits and truncate if longer
                    time_part = base_time + '.' + ms
                # Reconstruct timestamp
                timestamp_str = date_part + ' ' + time_part
                return datetime.strptime(timestamp_str, '%Y/%m/%d %H:%M:%S.%f')
    except (ValueError, IndexError):
        pass
    
    # Special handling for format: 2025/9/3 14:1:20:287 (milliseconds with colon, space-separated)
    try:
        timestamp_str = timestamp_str.strip()
        if ' ' in timestamp_str and ':' in timestamp_str and timestamp_str.count(':') == 3:
            # Split by space to separate date and time
            date_part, time_part = timestamp_str.split(' ', 1)
            # Replace the last colon (milliseconds separator) with a period
            time_parts = time_part.split(':')
            if len(time_parts) == 4:  # H:M:S:ms
                time_part = ':'.join(time_parts[:3]) + '.' + time_parts[3]
                # Pad milliseconds to 6 digits if needed
                if '.' in time_part:
                    base_time, ms = time_part.split('.')
                    ms = ms.ljust(6, '0')[:6]  # Pad to 6 digits and truncate if longer
                    time_part = base_time + '.' + ms
                # Reconstruct timestamp
                timestamp_str = date_part + ' ' + time_part
                return datetime.strptime(timestamp_str, '%Y/%m/%d %H:%M:%S.%f')
    except (ValueError, IndexError):
        pass
    
    # If no format matches, try to parse as Unix timestamp
    try:
        timestamp_float = float(timestamp_str.strip())
        return datetime.fromtimestamp(timestamp_float)
    except ValueError:
        pass
    
    raise ValueError(f"Unable to parse timestamp: {timestamp_str}")


def calculate_sampling_rate(timestamps: List[datetime]) -> float:
    """
    Calculate the average sampling rate from a list of timestamps.
    Returns the sampling rate in Hz.
    """
    if len(timestamps) < 2:
        return 0.0
    
    # Calculate time differences between consecutive timestamps
    time_diffs = []
    for i in range(1, len(timestamps)):
        diff = (timestamps[i] - timestamps[i-1]).total_seconds()
        time_diffs.append(diff)
    
    # Calculate average time difference
    avg_diff = sum(time_diffs) / len(time_diffs)
    
    # Return sampling rate in Hz
    return 1.0 / avg_diff if avg_diff > 0 else 0.0


def analyze_data_gaps(timestamps: List[datetime], expected_sampling_rate: float) -> List[dict]:
    """
    Analyze data gaps and count data points between gaps.
    
    Args:
        timestamps: List of parsed timestamps
        expected_sampling_rate: Expected sampling rate in Hz
    
    Returns:
        List of dictionaries containing gap analysis and point counts
    """
    if len(timestamps) < 2:
        return []
    
    expected_diff = 1.0 / expected_sampling_rate if expected_sampling_rate > 0 else 0
    gap_threshold = expected_diff * 1.5  # More than 50% longer than expected
    
    gap_analysis = []
    current_segment_start = 0
    current_segment_points = 0
    
    for i in range(1, len(timestamps)):
        time_diff = (timestamps[i] - timestamps[i-1]).total_seconds()
        
        if time_diff > gap_threshold:
            # Gap detected - record the previous segment
            if current_segment_points > 0:
                gap_analysis.append({
                    'segment_start_index': current_segment_start,
                    'segment_end_index': i - 1,
                    'point_count': current_segment_points,
                    'gap_index': i,
                    'gap_duration': time_diff,
                    'expected_diff': expected_diff,
                    'gap_size': time_diff - expected_diff
                })
            
            # Start new segment
            current_segment_start = i
            current_segment_points = 1
        else:
            # Normal interval - increment point count
            current_segment_points += 1
    
    # Record the final segment
    if current_segment_points > 0:
        gap_analysis.append({
            'segment_start_index': current_segment_start,
            'segment_end_index': len(timestamps) - 1,
            'point_count': current_segment_points,
            'gap_index': None,  # No gap after final segment
            'gap_duration': None,
            'expected_diff': expected_diff,
            'gap_size': None
        })
    
    return gap_analysis


def analyze_csv_data(csv_file_path: str, expected_sampling_rate: float = None) -> dict:
    """
    Analyze CSV data and compare actual vs expected data points.
    
    Args:
        csv_file_path: Path to the CSV file
        expected_sampling_rate: Expected sampling rate in Hz (optional)
    
    Returns:
        Dictionary with analysis results
    """
    if not os.path.exists(csv_file_path):
        raise FileNotFoundError(f"CSV file not found: {csv_file_path}")
    
    timestamps = []
    data_points = []
    
    try:
        with open(csv_file_path, 'r', newline='', encoding='utf-8') as csvfile:
            lines = csvfile.readlines()
            
            if not lines:
                raise ValueError("CSV file is empty")
            
            # Parse header
            header_line = lines[0].strip()
            # print(f"Debug - Header: {header_line}")
            
            # Check if header contains "W.G. Num:" and extract column names
            if 'W.G. Num:' in header_line and ',' in header_line:
                # Split the header manually to handle the W.G. Num prefix
                header_parts = header_line.split(',')
                if len(header_parts) >= 5:  # W.G. Num + 4 data columns
                    # Create a custom fieldnames list, ignoring the W.G. Num part
                    fieldnames = header_parts[1:]  # Skip the first part (W.G. Num)
                    # print(f"Debug - Extracted fieldnames: {fieldnames}")
                else:
                    raise ValueError(f"Unexpected header format: {header_line}")
            else:
                # Normal CSV processing
                fieldnames = header_line.split(',')
                # print(f"Debug - Normal fieldnames: {fieldnames}")
            
            # Validate expected columns
            expected_columns = ['Timestamp', 'Pressure [mbar]', 'Temp [deg C]', 'Battery [VDC]']
            
            if not all(col in fieldnames for col in expected_columns):
                print(f"Warning: Expected columns not found. Found: {fieldnames}")
                print("Proceeding with available columns...")
            
            # Process data rows
            for row_num, line in enumerate(lines[1:], start=2):  # Skip header, start at row 2
                try:
                    line = line.strip()
                    if not line:  # Skip empty lines
                        continue
                    
                    # Split the line by commas
                    values = line.split(',')
                    # print(f"Debug - Row {row_num}: {values}")
                    
                    # Create a dictionary mapping fieldnames to values
                    if len(values) >= len(fieldnames):
                        row = dict(zip(fieldnames, values[:len(fieldnames)]))
                    else:
                        print(f"Warning: Row {row_num} has insufficient columns: {values}")
                        continue
                    
                    # Extract and parse timestamp
                    if 'Timestamp' in row and row['Timestamp'].strip():
                        timestamp_str = row['Timestamp'].strip()
                        
                        # Check if we need to combine date and time from separate columns
                        if ':' not in timestamp_str and len(values) > 1:
                            # Date is in first column, time might be in second column
                            if len(values) > 1 and ':' in values[1]:
                                # Combine date and time
                                combined_timestamp = f"{timestamp_str},{values[1]}"
                                timestamp = parse_timestamp(combined_timestamp)
                                timestamps.append(timestamp)
                                data_points.append(row)
                            else:
                                print(f"Warning: Row {row_num} has incomplete timestamp: {timestamp_str}")
                                continue
                        else:
                            # Normal timestamp parsing
                            timestamp = parse_timestamp(timestamp_str)
                            timestamps.append(timestamp)
                            data_points.append(row)
                    else:
                        print(f"Warning: Row {row_num} missing timestamp")
                        continue
                        
                except ValueError as e:
                    print(f"Warning: Skipping row {row_num} due to timestamp parsing error: {e}")
                    continue
    
    except Exception as e:
        raise Exception(f"Error reading CSV file: {e}")
    
    if len(timestamps) < 2:
        raise ValueError("Need at least 2 data points to perform analysis")
    
    # Calculate time range
    start_time = min(timestamps)
    end_time = max(timestamps)
    time_range_seconds = (end_time - start_time).total_seconds()
    
    # Calculate actual sampling rate
    actual_sampling_rate = calculate_sampling_rate(timestamps)
    
    # Determine expected sampling rate
    if expected_sampling_rate is None:
        # Use calculated sampling rate as expected
        expected_sampling_rate = actual_sampling_rate
    
    # Calculate expected number of points
    expected_points = int(time_range_seconds * expected_sampling_rate) + 1  # +1 for inclusive count
    
    # Count actual points
    actual_points = len(data_points)
    
    # Calculate difference and percentage
    point_difference = actual_points - expected_points
    percentage_diff = (point_difference / expected_points * 100) if expected_points > 0 else 0
    
    # Analyze gaps in data (legacy format for backward compatibility)
    gaps = []
    for i in range(1, len(timestamps)):
        time_diff = (timestamps[i] - timestamps[i-1]).total_seconds()
        expected_diff = 1.0 / expected_sampling_rate if expected_sampling_rate > 0 else 0
        if time_diff > expected_diff * 1.5:  # More than 50% longer than expected
            gaps.append({
                'index': i,
                'time_diff': time_diff,
                'expected_diff': expected_diff,
                'gap_size': time_diff - expected_diff
            })
    
    # Analyze data segments between gaps
    gap_analysis = analyze_data_gaps(timestamps, expected_sampling_rate)
    
    return {
        'file_path': csv_file_path,
        'start_time': start_time,
        'end_time': end_time,
        'time_range_seconds': time_range_seconds,
        'actual_points': actual_points,
        'expected_points': expected_points,
        'point_difference': point_difference,
        'percentage_diff': percentage_diff,
        'expected_sampling_rate': expected_sampling_rate,
        'actual_sampling_rate': actual_sampling_rate,
        'gaps': gaps,
        'gap_analysis': gap_analysis,
        'data_completeness': (actual_points / expected_points * 100) if expected_points > 0 else 0
    }


def print_analysis_results(results: dict):
    """Print formatted analysis results."""
    print("=" * 60)
    print("CSV DATA POINT ANALYSIS RESULTS")
    print("=" * 60)
    print(f"File: {results['file_path']}")
    print(f"Time Range: {results['start_time']} to {results['end_time']}")
    print(f"Duration: {results['time_range_seconds']:.2f} seconds ({results['time_range_seconds']/3600:.2f} hours)")
    print()
    
    print("DATA POINT COMPARISON:")
    print(f"  Actual Points:     {results['actual_points']:,}")
    print(f"  Expected Points:   {results['expected_points']:,}")
    print(f"  Difference:        {results['point_difference']:+,}")
    print(f"  Percentage Diff:   {results['percentage_diff']:+.2f}%")
    print(f"  Data Completeness: {results['data_completeness']:.2f}%")
    print()
    
    print("SAMPLING RATE ANALYSIS:")
    print(f"  Expected Rate:     {results['expected_sampling_rate']:.4f} Hz")
    print(f"  Actual Rate:       {results['actual_sampling_rate']:.4f} Hz")
    print(f"  Rate Difference:   {results['actual_sampling_rate'] - results['expected_sampling_rate']:+.4f} Hz")
    print()
    
    if results['gaps']:
        print(f"DATA GAPS DETECTED ({len(results['gaps'])} gaps):")
        for i, gap in enumerate(results['gaps'][:10]):  # Show first 10 gaps
            print(f"  Gap {i+1}: Index {gap['index']}, "
                  f"Time diff: {gap['time_diff']:.2f}s "
                  f"(expected: {gap['expected_diff']:.2f}s, "
                  f"gap: +{gap['gap_size']:.2f}s)")
        if len(results['gaps']) > 10:
            print(f"  ... and {len(results['gaps']) - 10} more gaps")
        print()
    
    # Display data segments between gaps
    if results['gap_analysis']:
        print("DATA SEGMENTS BETWEEN GAPS:")
        print("  (Shows number of consecutive data points between gaps)")
        for i, segment in enumerate(results['gap_analysis']):
            if segment['gap_index'] is not None:
                print(f"  Segment {i+1}: {segment['point_count']} points "
                      f"(indices {segment['segment_start_index']}-{segment['segment_end_index']}) "
                      f"→ Gap at index {segment['gap_index']} "
                      f"(duration: {segment['gap_duration']:.2f}s)")
            else:
                print(f"  Final Segment {i+1}: {segment['point_count']} points "
                      f"(indices {segment['segment_start_index']}-{segment['segment_end_index']}) "
                      f"→ End of data")
        
        # Summary statistics
        point_counts = [seg['point_count'] for seg in results['gap_analysis']]
        if point_counts:
            print(f"\n  SEGMENT STATISTICS:")
            print(f"    Total segments: {len(point_counts)}")
            print(f"    Points per segment - Min: {min(point_counts)}, "
                  f"Max: {max(point_counts)}, "
                  f"Avg: {sum(point_counts)/len(point_counts):.1f}")
            print(f"    Total points in segments: {sum(point_counts)}")
        print()
    
    # Summary assessment
    print("ASSESSMENT:")
    if abs(results['percentage_diff']) < 1:
        print("  ✓ Data collection appears complete and consistent")
    elif abs(results['percentage_diff']) < 5:
        print("  ⚠ Minor data collection issues detected")
    else:
        print("  ✗ Significant data collection issues detected")
    
    if results['gaps']:
        print(f"  ⚠ {len(results['gaps'])} data gaps detected")
    else:
        print("  ✓ No significant data gaps detected")
    
    # Additional assessment based on segment analysis
    if results['gap_analysis']:
        point_counts = [seg['point_count'] for seg in results['gap_analysis']]
        if len(point_counts) > 1:
            avg_segment_size = sum(point_counts) / len(point_counts)
            expected_segment_size = results['expected_sampling_rate'] * 60  # Expected points per minute
            if avg_segment_size < expected_segment_size * 0.5:
                print("  ⚠ Data segments are shorter than expected - frequent interruptions")
            elif avg_segment_size > expected_segment_size * 2:
                print("  ✓ Data segments are longer than expected - good continuity")
        else:
            print("  ✓ Single continuous data segment detected")


def main():
    """Main function to handle command line arguments and run analysis."""
    parser = argparse.ArgumentParser(
        description="Analyze CSV sensor data to compare actual vs expected data points",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python csv_data_analyzer.py sensor_data.csv
  python csv_data_analyzer.py sensor_data.csv --sampling-rate 1.0
  python csv_data_analyzer.py sensor_data.csv -r 0.5
        """
    )
    
    parser.add_argument('csv_file', help='Path to the CSV file to analyze')
    parser.add_argument('-r', '--sampling-rate', type=float, 
                       help='Expected sampling rate in Hz (if not provided, will be calculated from data)')
    
    args = parser.parse_args()
    
    try:
        results = analyze_csv_data(args.csv_file, args.sampling_rate)
        print_analysis_results(results)
        
        # Exit with appropriate code
        if abs(results['percentage_diff']) > 5 or len(results['gaps']) > 0:
            sys.exit(1)  # Issues detected
        else:
            sys.exit(0)  # No significant issues
            
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
