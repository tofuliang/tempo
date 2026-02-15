package com.cappielloantonio.tempo.ui.fragment;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.media3.common.util.UnstableApi;

import com.cappielloantonio.tempo.R;
import com.cappielloantonio.tempo.databinding.FragmentToolbarBinding;
import com.cappielloantonio.tempo.service.AirPlaySessionManager;
import com.cappielloantonio.tempo.ui.activity.MainActivity;
import com.cappielloantonio.tempo.ui.dialog.AirPlayDevicePickerDialog;
import com.google.android.gms.cast.framework.CastButtonFactory;

@UnstableApi
public class ToolbarFragment extends Fragment {
    private static final String TAG = "ToolbarFragment";

    private FragmentToolbarBinding bind;
    private MainActivity activity;
    private MenuItem airplayMenuItem;

    public ToolbarFragment() {
        // Required empty public constructor
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setHasOptionsMenu(true);
    }

    @Override
    public void onCreateOptionsMenu(@NonNull Menu menu, @NonNull MenuInflater inflater) {
        super.onCreateOptionsMenu(menu, inflater);
        inflater.inflate(R.menu.main_page_menu, menu);
        CastButtonFactory.setUpMediaRouteButton(requireContext(), menu, R.id.media_route_menu_item);
        airplayMenuItem = menu.findItem(R.id.action_airplay);
        observeAirPlayState();
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        activity = (MainActivity) getActivity();

        bind = FragmentToolbarBinding.inflate(inflater, container, false);
        View view = bind.getRoot();

        return view;
    }

    @Override
    public boolean onOptionsItemSelected(@NonNull MenuItem item) {
        if (item.getItemId() == R.id.action_search) {
            activity.navController.navigate(R.id.searchFragment);
            return true;
        } else if (item.getItemId() == R.id.action_airplay) {
            new AirPlayDevicePickerDialog().show(getParentFragmentManager(), "AirPlayPicker");
            return true;
        } else if (item.getItemId() == R.id.action_settings) {
            activity.navController.navigate(R.id.settingsFragment);
            return true;
        }

        return false;
    }

    private void observeAirPlayState() {
        AirPlaySessionManager.getInstance().getState().observe(getViewLifecycleOwner(), state -> {
            if (airplayMenuItem == null) return;

            if (state != null && state != AirPlaySessionManager.STATE_DISCONNECTED) {
                airplayMenuItem.setIcon(R.drawable.ic_airplay_connected);
            } else {
                airplayMenuItem.setIcon(R.drawable.ic_airplay);
            }
        });
    }
}