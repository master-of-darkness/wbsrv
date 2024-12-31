import http from 'k6/http';
import { check, sleep } from 'k6';
import { Trend } from 'k6/metrics';

// Define custom metrics
let responseTimeTrend = new Trend('response_time');

// Configuration options for k6
export let options = {
    // Define the number of virtual users and the test duration
    stages: [
        { duration: '10s', target: 5000 },  // Ramp up to 50 users in 10 seconds
        { duration: '10s', target: 5000 },  // Stay at 50 users for 10 seconds
        { duration: '5s', target: 0 },    // Ramp down to 0 users in 5 seconds
    ],
    thresholds: {
        'http_req_duration': ['p(95)<500'],  // Ensure 95% of requests complete in under 500ms
    },
};

// Main test function
export default function () {
    let url = 'http://localhost:8080/';  // Replace with your server's URL
    let response = http.get(url);

    // Check the status code is 200
    check(response, {
        'is status 200': (r) => r.status === 200,
    });

    // Record the response time in the custom metric
    responseTimeTrend.add(response.timings.duration);

    // Sleep for a very short time to simulate rapid user requests
    sleep(0.1);  // Adjust this to simulate high request frequency
}
