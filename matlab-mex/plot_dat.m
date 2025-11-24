%==========================================================================
% Per Vices MatLab Implementation
% Copyright (C) 2025 Per Vices Corporation
%
% SPDX-ID: GPL-3.0
%
% This program is free software: you can redistribute it and/or modify
% it under the terms of the GNU General Public License as published by
% the Free Software Foundation, either version 3 of the License, or
% (at your option) any later version.
%
% This program is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
% GNU General Public License for more details.
%
% You should have received a copy of the GNU General Public License
% along with this program.  If not, see <http://www.gnu.org/licenses/>.
%==========================================================================
% This is a common IQ plot script from file.
%==========================================================================
f = fopen('my_data.dat', 'rb');
data = fread(f, [1, Inf], 'short'); % Read all data as 16-bit integers
fclose(f);

% Separate I and Q components
I = data(1:2:end);
Q = data(2:2:end);

% Create complex IQ data
IQData = I + 1i * Q;

% Plot the first 1000 points of the time-domain signal
figure(1);
plot(real(IQData(1:1000)), 'b');
hold on;
plot(imag(IQData(1:1000)), 'g');
legend('Inphase signal', 'Quadrature signal');
title('IQ Data for the first 1000 points of acquired signal');
xlabel('Sample number');
ylabel('Voltage');   
