import React from 'react';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend
);

// Card Component
function Card({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div
      data-slot="card"
      className={className}
      {...props}
    />
  );
}

function CardHeader({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div data-slot="card-header" className={className} {...props} />
  );
}

function CardTitle({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div data-slot="card-title" className={className} {...props} />
  );
}

function CardContent({ className, ...props }: React.ComponentProps<"div">) {
  return (
    <div data-slot="card-content" className={className} {...props} />
  );
}

interface SensorData {
  temperature: number;
  humidity: number;
  airQuality: number;
  timestamp: string;
  unixTime?: number;
}

type TimeRange = '1hr' | '3hr' | '12hr' | '24hr' | 'all';

interface AppProps {
  sensor1Data: SensorData[];
  sensor2Data: SensorData[];
  lastSyncTime: string;
  isOnline: boolean;
  timeRange: TimeRange;
  onTimeRangeChange: (range: TimeRange) => void;
}

const TIME_RANGES: { value: TimeRange; label: string }[] = [
  { value: '1hr', label: '1hr' },
  { value: '3hr', label: '3hr' },
  { value: '12hr', label: '12hr' },
  { value: '24hr', label: '24hr' },
  { value: 'all', label: 'All' },
];

export default function App({ sensor1Data, sensor2Data, lastSyncTime, isOnline, timeRange, onTimeRangeChange }: AppProps) {
  const [showSensor1, setShowSensor1] = React.useState(true);
  const [showSensor2, setShowSensor2] = React.useState(true);

  // Get all unique timestamps from both sensors
  const allTimestamps = [
    ...sensor1Data.map(d => d.timestamp),
    ...sensor2Data.map(d => d.timestamp)
  ].sort();

  // Use all available data points (no limit since API filters by time range)
  const labels = allTimestamps;

  const getSensorValues = (data: SensorData[], key: keyof SensorData) => {
    return labels.map(ts => {
      const point = data.find(d => d.timestamp === ts);
      return point ? point[key] : null;
    });
  };

  const chartData = {
    labels,
    datasets: [
      {
        label: 'Sensor 1 - Air Quality',
        data: showSensor1 ? getSensorValues(sensor1Data, 'airQuality') : [],
        borderColor: '#22c55e',
        backgroundColor: '#22c55e',
        tension: 0.25,
        borderWidth: 2,
        pointRadius: 3,
      },
      {
        label: 'Sensor 2 - Air Quality',
        data: showSensor2 ? getSensorValues(sensor2Data, 'airQuality') : [],
        borderColor: '#f59e0b',
        backgroundColor: '#f59e0b',
        tension: 0.25,
        borderWidth: 2,
        pointRadius: 3,
      },
    ],
  };

  const chartOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: { display: true, position: 'bottom' as const },
      title: {
        display: true,
        text: `Air Quality Over Time (${timeRange === 'all' ? 'All Time' : timeRange})`,
        font: { size: 16, family: 'system-ui' },
      },
    },
    scales: {
      x: {
        grid: { color: 'rgba(0,0,0,0.1)' },
        ticks: { maxRotation: 45, minRotation: 45 }
      },
      y: {
        grid: { color: 'rgba(0,0,0,0.1)' },
        min: 0,
      },
    },
  };

  // Get latest readings
  const latestSensor1 = sensor1Data[sensor1Data.length - 1];
  const latestSensor2 = sensor2Data[sensor2Data.length - 1];

  // Status indicator
  const getAQStatus = (aq: number) => {
    if (!aq || aq < 200) return { label: 'Good', color: '#22c55e' };
    if (aq < 500) return { label: 'Moderate', color: '#f59e0b' };
    if (aq < 1000) return { label: 'Unhealthy', color: '#ef4444' };
    return { label: 'Very Unhealthy', color: '#7f1d1d' };
  };

  return (
    <div className="dashboard">
      {/* Header */}
      <header className="header">
        <h1>Air Monitor</h1>
        <div className="header-info">
          <span className="sync-time">Last synced: {lastSyncTime || 'Never'}</span>
          <span className={`status ${isOnline ? 'online' : 'offline'}`}>
            {isOnline ? '● Online' : '○ Offline'}
          </span>
        </div>
      </header>

      {/* AQ Legend/Tags */}
      <section className="aq-tags">
        <div className="tag" style={{ borderColor: '#22c55e' }}>
          <span className="tag-color" style={{ background: '#22c55e' }}></span>
          <span>Good (AQ &lt; 200)</span>
        </div>
        <div className="tag" style={{ borderColor: '#f59e0b' }}>
          <span className="tag-color" style={{ background: '#f59e0b' }}></span>
          <span>Moderate (AQ 200-500)</span>
        </div>
        <div className="tag" style={{ borderColor: '#ef4444' }}>
          <span className="tag-color" style={{ background: '#ef4444' }}></span>
          <span>Unhealthy (AQ 500-1000)</span>
        </div>
        <div className="tag" style={{ borderColor: '#7f1d1d' }}>
          <span className="tag-color" style={{ background: '#7f1d1d' }}></span>
          <span>Very Unhealthy (AQ &gt; 1000)</span>
        </div>
      </section>

      {/* Current Readings */}
      <section className="current-readings">
        <Card className="reading-card sensor1">
          <CardHeader>
            <CardTitle>Sensor 1</CardTitle>
          </CardHeader>
          <CardContent>
            {latestSensor1 ? (
              <>
                <div className="reading-value" style={{ color: getAQStatus(latestSensor1.airQuality).color }}>
                  {latestSensor1.airQuality}
                </div>
                <div className="reading-status" style={{ color: getAQStatus(latestSensor1.airQuality).color }}>
                  {getAQStatus(latestSensor1.airQuality).label}
                </div>
                <div className="reading-details">
                  <span>Temp: {latestSensor1.temperature?.toFixed(1)}°C</span>
                  <span>Hum: {latestSensor1.humidity?.toFixed(1)}%</span>
                </div>
              </>
            ) : (
              <div className="no-data">No data</div>
            )}
          </CardContent>
        </Card>

        <Card className="reading-card sensor2">
          <CardHeader>
            <CardTitle>Sensor 2</CardTitle>
          </CardHeader>
          <CardContent>
            {latestSensor2 ? (
              <>
                <div className="reading-value" style={{ color: getAQStatus(latestSensor2.airQuality).color }}>
                  {latestSensor2.airQuality}
                </div>
                <div className="reading-status" style={{ color: getAQStatus(latestSensor2.airQuality).color }}>
                  {getAQStatus(latestSensor2.airQuality).label}
                </div>
                <div className="reading-details">
                  <span>Temp: {latestSensor2.temperature?.toFixed(1)}°C</span>
                  <span>Hum: {latestSensor2.humidity?.toFixed(1)}%</span>
                </div>
              </>
            ) : (
              <div className="no-data">No data</div>
            )}
          </CardContent>
        </Card>
      </section>

      {/* Time Range Selector */}
      <section className="time-range-selector">
        {TIME_RANGES.map((range) => (
          <button
            key={range.value}
            className={`time-range-btn ${timeRange === range.value ? 'active' : ''}`}
            onClick={() => onTimeRangeChange(range.value)}
          >
            {range.label}
          </button>
        ))}
      </section>

      {/* Chart */}
      <Card className="chart-card">
        <CardContent className="chart-content">
          <Line data={chartData} options={chartOptions} />
        </CardContent>
      </Card>

      {/* Toggle Buttons */}
      <section className="chart-controls">
        <button
          className={`toggle-btn ${showSensor1 ? 'active' : ''}`}
          onClick={() => setShowSensor1(!showSensor1)}
          style={{ borderColor: showSensor1 ? '#22c55e' : '#ccc' }}
        >
          Sensor 1
        </button>
        <button
          className={`toggle-btn ${showSensor2 ? 'active' : ''}`}
          onClick={() => setShowSensor2(!showSensor2)}
          style={{ borderColor: showSensor2 ? '#f59e0b' : '#ccc' }}
        >
          Sensor 2
        </button>
      </section>
    </div>
  );
}