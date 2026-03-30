import { StrictMode, useState, useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import './App.css'
import App from './App.tsx'

interface SensorData {
  temperature: number;
  humidity: number;
  airQuality: number;
  timestamp: string;
  unixTime?: number;
}

type TimeRange = '1hr' | '3hr' | '12hr' | '24hr' | 'all';

const TIME_RANGE_SECONDS: Record<TimeRange, number> = {
  '1hr': 3600,
  '3hr': 10800,
  '12hr': 43200,
  '24hr': 86400,
  'all': 0
};

// Your Firebase Realtime Database URL
const FIREBASE_DB_URL = 'https://air-quality-monitor-8aac4-default-rtdb.firebaseio.com';

// Fetch data from Firebase using REST API with time range
async function fetchSensorData(sensorId: number, timeRange: TimeRange): Promise<SensorData[]> {
  try {
    const now = Math.floor(Date.now() / 1000);
    const cutoff = timeRange === 'all' ? 0 : now - TIME_RANGE_SECONDS[timeRange];

    let url = `${FIREBASE_DB_URL}/sensor_readings/${sensorId}.json`;

    const response = await fetch(url, {
      method: 'GET',
      headers: {
        'Content-Type': 'application/json',
      }
    });

    if (!response.ok) {
      console.error('Fetch error:', response.status, await response.text());
      throw new Error(`Failed to fetch: ${response.status}`);
    }

    const data = await response.json();
    if (!data) return [];

    // Convert Firebase object to array - keys are the unix timestamps
    let readings: SensorData[] = Object.entries(data).map(([key, value]: [string, any]) => ({
      temperature: value.temperature || 0,
      humidity: value.humidity || 0,
      airQuality: value.airQuality || 0,
      timestamp: value.timestamp || '',
      unixTime: parseInt(key) || 0,
    }));

    // Filter by time range client-side
    if (cutoff > 0) {
      readings = readings.filter(r => (r.unixTime || 0) >= cutoff);
    }

    // Sort by unixTime and take last 100
    return readings.sort((a, b) => (a.unixTime || 0) - (b.unixTime || 0)).slice(-100);
  } catch (error) {
    console.error('Error fetching sensor data:', error);
    return [];
  }
}

// Fetch latest reading to determine online status
async function fetchLatestData(): Promise<{ lastSync: string; isOnline: boolean }> {
  try {
    // Fetch both sensor latest readings
    const [res1, res2] = await Promise.all([
      fetch(`${FIREBASE_DB_URL}/latest/1.json`),
      fetch(`${FIREBASE_DB_URL}/latest/2.json`)
    ]);

    let latestUnixTime = 0;
    let latestTimestamp = 'No data';

    // Check sensor 1
    if (res1.ok) {
      const data1 = await res1.json();
      if (data1 && data1.lastUnixTime && data1.lastUnixTime > latestUnixTime) {
        latestUnixTime = data1.lastUnixTime;
        latestTimestamp = data1.lastUpdate || 'Unknown';
      }
    }

    // Check sensor 2
    if (res2.ok) {
      const data2 = await res2.json();
      if (data2 && data2.lastUnixTime && data2.lastUnixTime > latestUnixTime) {
        latestUnixTime = data2.lastUnixTime;
        latestTimestamp = data2.lastUpdate || 'Unknown';
      }
    }

    if (latestUnixTime > 0) {
      const now = Math.floor(Date.now() / 1000);
      const diffSecs = now - latestUnixTime;
      const diffMins = diffSecs / 60;

      console.log(latestTimestamp, diffMins)

      return {
        lastSync: latestTimestamp,
        isOnline: diffMins < 5
      };
    }

    return { lastSync: 'No data', isOnline: false };
  } catch (error) {
    console.error('Error fetching status:', error);
    return { lastSync: 'Error', isOnline: false };
  }
}

function Root() {
  const [sensor1Data, setSensor1Data] = useState<SensorData[]>([]);
  const [sensor2Data, setSensor2Data] = useState<SensorData[]>([]);
  const [lastSyncTime, setLastSyncTime] = useState('Loading...');
  const [isOnline, setIsOnline] = useState(false);
  const [loading, setLoading] = useState(true);
  const [timeRange, setTimeRange] = useState<TimeRange>('1hr');

  const loadData = async () => {
    try {
      const [s1, s2, status] = await Promise.all([
        fetchSensorData(1, timeRange),
        fetchSensorData(2, timeRange),
        fetchLatestData()
      ]);

      setSensor1Data(s1);
      setSensor2Data(s2);
      setLastSyncTime(status.lastSync);
      setIsOnline(status.isOnline);
    } catch (error) {
      console.error('Error loading data:', error);
    } finally {
      setLoading(false);
    }
    console.log(lastSyncTime, isOnline)
  };

  useEffect(() => {
    setLoading(true);
    loadData();
  }, [timeRange]);

  useEffect(() => {
    // Refresh every 10 seconds (only if not loading)
    if (!loading) {
      fetchLatestData()
      const interval = setInterval(loadData, 10000);
      return () => clearInterval(interval);
    }
  }, [loading, timeRange]);

  const handleTimeRangeChange = (range: TimeRange) => {
    setTimeRange(range);
  };

  if (loading) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100vh', fontFamily: 'system-ui' }}>
        <p>Loading sensor data...</p>
      </div>
    );
  }

  return (
    <StrictMode>
      <App
        sensor1Data={sensor1Data}
        sensor2Data={sensor2Data}
        lastSyncTime={lastSyncTime}
        isOnline={isOnline}
        timeRange={timeRange}
        onTimeRangeChange={handleTimeRangeChange}
      />
    </StrictMode>
  );
}

createRoot(document.getElementById('root')!).render(<Root />);