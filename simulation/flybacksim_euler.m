% New model for LM5022 that uses Euler integration of transformer currents
% and voltages instead of cycle by cycle ideal calculations, to validate
% flybacksim

PRODUCE_PLOTS = 1;

%LM5022 params
Dmax = 0.9; % Maximum duty cycle
Vcl_thresh = 0.5; % Current limit thresh
Icl = 45e-6; % Current limit injected current peak
Rcl_int = 2e3; % internal current limit resistance

% Capacitor load Specs
Cload = 200e-6; % Output capacitance
ESRload = 2.1e-3; % ESR of output capacitor; 2P2S configuration means effective ESR is the same as one of the 4 caps.

% Bleed Resistor
Rload = 286e3; % Bleed resistor

% Output Diode Forward Voltage
Vf = 1.2; % Output Diode Forward Voltage

% Design details
N1 = 2; % Transformer turns
N2 = 40;
Vin = 15; % 12 < Vin < 16.8
fsw = 250e3;

% Transformer specs
Lpri = 1.2e-6;
Lpri_leakage = 80e-9;
Rm = N1*N1 / Lpri;
Lsec = N2*N2 / Rm;
%Lsec = 41e-6; % Todo: figure out if this is true because if so very strange.

% Current Limit Settings
Rsns = 100e-3; % Primary side switch current sense resistor
Rs2 = 1e3; % Rs2 and Rs1 are as referred to in LM5022 datasheet. 
Rs1 = 100;

% Snubber selection
Rsn = 500;
Csn = 22e-6;

%Simulation outputs
t_end = 0.001;
Tsw = 1/fsw;
Tmax_dutycycle = Dmax * Tsw;
steps_per_swpd = 1000;
t_step = Tsw / steps_per_swpd;

Vload = zeros(fix([t_end / t_step + 1 1])); % Output load voltage
Vload(1) = 200;
Iload = zeros(fix([t_end / t_step + 1 1])); % Output load current
I1 = zeros(fix([t_end / t_step + 1 1])); % Primary current
Phi_m = zeros(fix([t_end / t_step + 1 1])); % Core flux
Ton = zeros(fix([t_end / t_step + 1 1])); % Time since last switch rising cycle. Continues counting when the switch turns off
Vcl = zeros(fix([t_end / t_step + 1 1]));
Don = zeros(fix([t_end / Tsw + 1 1])); % Duty cycle per cycle

t_charged = 0;

t = 0:t_step:t_end;

T1 = 1;
k = 1;
cyclenum = 1;
while (t(k) < t(end))
    % Cycle counter
    if (mod(k, steps_per_swpd) == 0)
        cyclenum = cyclenum + 1;
        T1 = 1;
        Ton(k) = 0;
    end

    % Duty cycle maximum limit
    if (T1 && Ton(k) > Tsw*Dmax)
        T1 = 0;
        Don(cyclenum) = Dmax;
    end

    % Current limit
    Vcl(k) = I1(k) * Rsns + T1 * (Rs1 + Rs2 + Rcl_int) * Icl * (Ton(k) / Tsw);
    if (Vcl(k) > Vcl_thresh)
        T1 = 0;
        Don(cyclenum) = Ton(k) / Tsw;
    end

    % Euler
    Transfer_matrix = [1                             (1-T1) * t_step / N2;
                       -1*(1-T1)*t_step*Rm/(N2*Cload) 1+t_step/(Rload*Cload)];
    next_vi = Transfer_matrix \ [Phi_m(k) + T1*t_step*Vin/N1 - (1-T1)*t_step*Vf/N2; Vload(k)];
    Phi_m(k + 1) = max(next_vi(1), 0);
    Vload(k + 1) = next_vi(2);
    I1(k + 1) = T1 * Phi_m(k + 1) * Rm / N1;
    Iload(k + 1) = (1-T1) * Phi_m(k + 1) * Rm / N2;

    Ton(k + 1) = Ton(k) + t_step;
    k = k + 1;
    disp(t(k));
end