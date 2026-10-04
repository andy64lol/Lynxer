const express = require('express');
const fs = require('fs');
const path = require('path');

const app = express();
const port = process.env.PORT || 3000;

// Middleware to parse JSON requests
app.use(express.json());

// Load the registry configuration
const configPath = path.join(__dirname, 'registry.json');
const config = JSON.parse(fs.readFileSync(configPath, 'utf-8'));

// API endpoint to resolve package names to GitHub repositories
app.post('/api/resolve', (req, res) => {
  const { name } = req.body;

  if (!name) {
    return res.status(400).json({ error: 'Package name is required' });
  }

  const packageInfo = config.packages[name];

  if (!packageInfo) {
    return res.status(404).json({ error: `Package '${name}' not found` });
  }

  res.json({
    repository: packageInfo.repository,
    owner: packageInfo.owner
  });
});

// Start the server
app.listen(port, () => {
  console.log(`Registry API running on port ${port}`);
});