Page({
  data: {
    loading: false,
    hasData: false,
    heartRateData: [],
    timeLabels: [],
    avgHeartRate: '--',
    maxHeartRate: '--',
    minHeartRate: '--'
  },

  onLoad() {
    this.loadHistoryData();
  },

  onShow() {
    this.loadHistoryData();
  },

  loadHistoryData() {
    this.setData({ loading: true });
    try {
      const history = wx.getStorageSync('heart_rate_history') || [];
      if (history.length === 0) {
        this.setData({ hasData: false, loading: false });
        return;
      }

      const rawValues = history.map(item => item.value);
      const validValues = rawValues.filter(v => v !== 0);

      if (validValues.length === 0) {
        this.setData({ hasData: false, loading: false });
        return;
      }

      const labels = history.map(item => {
        const date = new Date(item.timestamp);
        return `${date.getMonth() + 1}/${date.getDate()} ${date.getHours().toString().padStart(2, '0')}:${date.getMinutes().toString().padStart(2, '0')}`;
      });

      const sum = validValues.reduce((a, b) => a + b, 0);
      const avg = (sum / validValues.length).toFixed(1);
      const max = Math.max(...validValues);
      const min = Math.min(...validValues);

      this.setData({
        heartRateData: rawValues,
        timeLabels: labels,
        hasData: true,
        avgHeartRate: avg,
        maxHeartRate: max,
        minHeartRate: min,
        loading: false
      });

      this.drawLineChart(rawValues, labels);
    } catch (err) {
      console.error(err);
      this.setData({ hasData: false, loading: false });
    }
  },

  drawLineChart(data, labels) {
    const ctx = wx.createCanvasContext('heartRateChart');
    const query = wx.createSelectorQuery();
    query.select('.chart').boundingClientRect(rect => {
      if (rect) this.renderChart(ctx, data, labels, rect.width, rect.height);
    }).exec();
  },

  renderChart(ctx, data, labels, width, height) {
    if (!width || !height || data.length === 0) return;

    // 找出所有有效点（值不为 0）
    const validPoints = data
      .map((val, idx) => ({ idx, val }))
      .filter(item => item.val !== 0);

    if (validPoints.length === 0) return;

    const validValues = validPoints.map(p => p.val);
    let maxValue = Math.max(...validValues);
    let minValue = Math.min(...validValues);

    if (maxValue === minValue) {
      maxValue += 1;
      minValue -= 1;
    }

    const padding = { top: 30, right: 30, bottom: 40, left: 50 };
    const chartWidth = width - padding.left - padding.right;
    const chartHeight = height - padding.top - padding.bottom;
    const valueRange = maxValue - minValue;
    const yScale = chartHeight / valueRange;

    // 有效点的索引范围
    const minIdx = validPoints[0].idx;
    const maxIdx = validPoints[validPoints.length - 1].idx;
    const idxRange = maxIdx - minIdx;

    ctx.clearRect(0, 0, width, height);
    ctx.setFillStyle('#ffffff');
    ctx.fillRect(0, 0, width, height);

    // 坐标轴
    ctx.beginPath();
    ctx.setStrokeStyle('#e2e8f0');
    ctx.setLineWidth(1);
    ctx.moveTo(padding.left, padding.top);
    ctx.lineTo(padding.left, height - padding.bottom);
    ctx.lineTo(width - padding.right, height - padding.bottom);
    ctx.stroke();

    // Y 轴刻度
    ctx.setFontSize(10);
    ctx.setFillStyle('#94a3b8');
    for (let i = 0; i <= 4; i++) {
      const val = minValue + (valueRange * i / 4);
      const y = height - padding.bottom - (val - minValue) * yScale;
      ctx.fillText(Math.round(val), padding.left - 30, y);
      ctx.beginPath();
      ctx.setStrokeStyle('#f1f5f9');
      ctx.moveTo(padding.left, y);
      ctx.lineTo(width - padding.right, y);
      ctx.stroke();
    }

    // X 轴标签（只显示有效范围内的标签，均匀选取 6 个）
    const labelStep = Math.max(1, Math.ceil((maxIdx - minIdx + 1) / 6));
    for (let i = minIdx; i <= maxIdx; i += labelStep) {
      const relIdx = i - minIdx;
      const x = idxRange === 0
        ? padding.left + chartWidth / 2
        : padding.left + (relIdx / idxRange) * chartWidth;
      ctx.fillText(labels[i] || '', x - 20, height - padding.bottom + 20);
    }

    // 折线（只连接有效点，X 坐标基于有效范围映射）
    ctx.beginPath();
    ctx.setStrokeStyle('#f5576c');
    ctx.setLineWidth(3);
    validPoints.forEach((point, index) => {
      const relIdx = point.idx - minIdx;
      const x = idxRange === 0
        ? padding.left + chartWidth / 2
        : padding.left + (relIdx / idxRange) * chartWidth;
      const y = height - padding.bottom - (point.val - minValue) * yScale;
      if (index === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    });
    ctx.stroke();

    ctx.draw();
  }
});